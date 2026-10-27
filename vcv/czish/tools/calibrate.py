#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Prepare CZ-101 captures and report measurements; never changes DSP parameters."""
import argparse
import csv
import hashlib
import io
import json
import math
from pathlib import Path
import subprocess
import zipfile

import numpy as np
import scipy
from scipy.io import wavfile
from scipy.ndimage import uniform_filter1d
from scipy.signal import correlate, correlation_lags, resample_poly

ROOT = Path(__file__).resolve().parents[1]
ARCHIVE = ROOT / 'tests/hardware/cz101-capture-fixtures.zip'
MODELS = ('native', 'instant', 'syllabic')
CHAIN_FIELDS = ('keyboard', 'audio_interface', 'input_type', 'input_gain',
                'keyboard_output', 'trim_status', 'processing')
QUICK_PREFIXES = ('a-', 'b-', 'c-', 'd-', 'e-', 'i-')


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def write_json(path, value):
    Path(path).write_text(json.dumps(value, indent=2, allow_nan=False) + '\n')


def vlq(value):
    out = [value & 127]
    while value >> 7:
        value >>= 7
        out.insert(0, (value & 127) | 128)
    return bytes(out)


def fixture_midi(data):
    """Read our fixed SMF0/960 PPQN/120 BPM fixtures, rejecting other MIDI."""
    if data[:14] != bytes.fromhex('4d546864000000060000000103c0') or data[14:18] != b'MTrk':
        raise ValueError('Expected the committed SMF0 / 960 PPQN capture format')
    if len(data) < 22 or int.from_bytes(data[18:22], 'big') != len(data) - 22:
        raise ValueError('Truncated MIDI track')
    offset, tick, active, events = 22, 0, {}, []

    def byte():
        nonlocal offset
        if offset >= len(data):
            raise ValueError('Truncated MIDI event')
        result = data[offset]
        offset += 1
        return result

    def variable():
        result = 0
        for _ in range(4):
            value = byte()
            result = result * 128 + (value & 127)
            if value < 128:
                return result
        raise ValueError('MIDI delta exceeds four bytes')

    while offset < len(data):
        delta_start = offset
        tick += variable()
        status = byte()
        if status == 255:
            kind, length = byte(), variable()
            payload = bytes(byte() for _ in range(length))
            if kind == 81 and payload != bytes.fromhex('07a120'):
                raise ValueError('Only the fixture tempo of 120 BPM is supported')
            if kind == 47:
                if payload or active or offset != len(data):
                    raise ValueError('Missing note-off or invalid MIDI end event')
                return events, tick / 1920, delta_start
            if kind not in (1, 81):
                raise ValueError('Unexpected fixture meta event')
        elif status in (128, 144):
            note, velocity = byte(), byte()
            if note >= 128 or velocity >= 128:
                raise ValueError('Invalid note data')
            on = status == 144 and velocity > 0
            if on:
                if note in active or len(active) >= 8:
                    raise ValueError('Overlapping note or too many voices')
                slot = next(i for i in range(8) if i not in active.values())
                active[note] = slot
            else:
                if note not in active:
                    raise ValueError('Unmatched note-off')
                slot = active.pop(note)
            events.append({'seconds': tick / 1920, 'slot': slot, 'note': note, 'gate': int(on)})
        else:
            raise ValueError('Unsupported MIDI event (this is not a general MIDI importer)')
    raise ValueError('Missing MIDI end event')


def padded_midi(data):
    _, _, end = fixture_midi(data)
    # Two seconds before the original sequence, one extra second before its EOT.
    # The MIDI default tempo is already 120 BPM during the leading text event.
    first = vlq(3840) + bytes.fromhex('ff0100')
    offset, old_delta = end, 0
    while True:
        value = data[offset]
        offset += 1
        old_delta = old_delta * 128 + (value & 127)
        if value < 128:
            break
    track = first + data[22:end] + vlq(old_delta + 1920) + data[offset:]
    padded = data[:18] + len(track).to_bytes(4, 'big') + track
    events, duration, _ = fixture_midi(padded)
    return padded, events, duration


def schedule(path, rate, duration, events):
    frames = round(rate * duration)
    rows = [f'CZSEQ1 {rate} {frames}']
    rows += [f"{round(e['seconds'] * rate)} {e['slot']} {e['note']} {e['gate']}" for e in events]
    path.write_text('\n'.join(rows) + '\n')


def holdout(name):
    # Declared before seeing data, not selected by the resulting residuals.
    return (name in ('b-cosine-level-75', 'd-poly-4', 'd-poly-8', 'e-saw-note-84', 'i-ring-pair')
            or name.startswith('f-') and name.endswith(('-33', '-77'))
            or name.startswith('g-') and name.endswith('-75')
            or name.startswith('h-key-5-'))


def prepare(args):
    directory = args.directory.resolve()
    if directory.exists() and any(directory.iterdir()):
        raise ValueError('Session directory must be new or empty; existing takes are never overwritten')
    renderer = args.renderer.resolve()
    if not renderer.is_file():
        raise ValueError('Build the renderer first: make -C vcv/czish/tests render')
    with zipfile.ZipFile(ARCHIVE) as archive:
        rows = list(csv.DictReader(io.StringIO(archive.read('manifest.tsv').decode()), delimiter='\t'))
        if args.only:
            names = set(args.only)
            if names - {row['name'] for row in rows}:
                raise ValueError('Unknown fixture name')
            rows = [row for row in rows if row['name'] in names]
        elif args.set == 'quick':
            rows = [row for row in rows if row['name'].startswith(QUICK_PREFIXES)]
        for sub in ('inputs', 'references', 'recordings'):
            (directory / sub).mkdir(parents=True, exist_ok=True)
        try:
            commit = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
            dirty = bool(subprocess.check_output(['git', 'status', '--porcelain', '--untracked-files=no'],
                                                cwd=ROOT, text=True).strip())
        except (subprocess.CalledProcessError, FileNotFoundError):
            commit, dirty = None, None
        session = {'schema': 1, 'purpose': 'measurement, not hardware validation',
                   'fixture_archive_sha256': sha(ARCHIVE), 'renderer_sha256': sha(renderer),
                   'source_commit': commit, 'source_dirty': dirty,
                   'analysis_versions': {'numpy': np.__version__, 'scipy': scipy.__version__},
                   'chain': {field: '' for field in CHAIN_FIELDS},
                   'recording_channel': None, 'expected_sample_rate': args.rate, 'cases': []}
        session['chain']['processing'] = 'none (confirm before capture)'
        for row in rows:
            name = row['name']
            syx = directory / 'inputs' / (name + '.syx')
            syx.write_bytes(archive.read(name + '.syx'))
            data, events, duration = padded_midi(archive.read(name + '.mid'))
            mid = directory / 'inputs' / (name + '.mid')
            mid.write_bytes(data)
            case = {'name': name, 'partition': 'holdout' if holdout(name) else 'fit',
                    'description': row['description'], 'duration_seconds': duration, 'events': events,
                    'recording': f'recordings/{name}.wav', 'channel': None,
                    'offset_seconds': None, 'references': {}, 'input_sha256': {}}
            for path in (syx, mid):
                case['input_sha256'][str(path.relative_to(directory))] = sha(path)
            for model in MODELS:
                rate = 40000 if model == 'native' else args.rate
                timing = directory / 'inputs' / f'{name}-{rate}.tsv'
                schedule(timing, rate, duration, events)
                output = directory / 'references' / f'{name}-{model}.wav'
                reconstruction = getattr(args, 'reconstruction', 'hold')
                subprocess.run([str(renderer), '--events', str(syx), str(timing), str(output), model,
                                reconstruction], check=True)
                case['references'][model] = {'path': str(output.relative_to(directory)), 'sha256': sha(output),
                                            'reconstruction': 'raw' if model == 'native' else reconstruction}
                case['input_sha256'][str(timing.relative_to(directory))] = sha(timing)
            session['cases'].append(case)
            # Retain progress if a long full-set render is interrupted.
            write_json(directory / 'session.json', session)
            print(f"Prepared {name} ({duration:g} s, {case['partition']})", flush=True)
    (directory / 'START-HERE.txt').write_text(
        'Fill in chain and recording_channel in session.json. No trim is required by this tool.\n'
        'Load each inputs/*.syx with a CZ-compatible librarian, then play its matching MIDI.\n'
        'The MIDI adds 2 s of lead-in and at least 2 s after the final note-off.\n'
        'Record each complete clip to its recordings/*.wav name, fixed gains, no processing.\n'
        'Use the matching padded MIDI, not the unpadded archive clip.\n'
        'Preserve the leading silence; record a-silence with the same chain.\n'
        'offset_seconds, if supplied, maps reference time t to recording time t + offset.\n'
        'Run calibrate.py analyze SESSION to write report.json and report.md.\n'
        'Automatic alignment is phase-ambiguous for periodic tones and is not latency.\n')
    (directory / 'recordings/README.txt').write_text(
        'Put hardware WAVs here under the fixture names listed in session.json.\n'
        'No hardware audio is included in this prepared session.\n')
    print(f'{len(rows)} cases ready in {directory}; no hardware recordings have been made')


def load_audio(path, channel=None):
    rate, data = wavfile.read(path)
    if rate < 8000 or rate > 384000 or data.ndim not in (1, 2) or len(data) < 2:
        raise ValueError('Unsupported or empty WAV')
    if len(data) > rate * 310:
        raise ValueError('Capture exceeds 310 seconds; split it into fixture takes')
    channels = data.shape[1] if data.ndim == 2 else 1
    if channel is None and channels != 1:
        raise ValueError('Stereo/multichannel WAV needs an explicit zero-based channel in session.json')
    if channel is not None and (type(channel) is not int or not 0 <= channel < channels):
        raise ValueError('Invalid zero-based recording channel')
    if data.ndim == 2:
        data = data[:, 0 if channel is None else channel]
    if data.dtype.kind == 'u':
        midpoint = 2 ** (data.dtype.itemsize * 8 - 1)
        x = (data.astype(np.float64) - midpoint) / midpoint
    elif data.dtype.kind == 'i':
        # scipy left-justifies 24-bit PCM in int32; scale by its container width.
        x = data.astype(np.float64) / 2 ** (data.dtype.itemsize * 8 - 1)
    elif data.dtype.kind == 'f':
        x = data.astype(np.float64)
    else:
        raise ValueError('Unsupported WAV sample format')
    if not np.all(np.isfinite(x)):
        raise ValueError('WAV contains non-finite samples')
    return int(rate), x


def db(value):
    # null, rather than -Infinity, keeps reports strict JSON.
    return 20 * math.log10(value) if value > 0 else None


def rms(x):
    return float(np.sqrt(np.mean(np.square(x)))) if len(x) else 0.


def resample(x, source, target):
    divisor = math.gcd(source, target)
    return x if source == target else resample_poly(x, target // divisor, source // divisor,
                                                   window=('kaiser', 10.))


def auto_offset(reference, recording, rate, max_lag):
    """Envelope alignment, then a local waveform fit. Diagnostic, not physical latency."""
    block = max(1, round(rate * .005))
    limit = min(len(reference), round(rate * 30))
    observed_limit = min(len(recording), limit + round(rate * max_lag))

    def envelope(x):
        size = len(x) // block
        x = x[:size * block].reshape(size, block)
        power = np.sqrt(np.mean(x * x, axis=1))
        return np.diff(uniform_filter1d(power, 5), prepend=0.)

    a, b = envelope(reference[:limit]), envelope(recording[:observed_limit])
    if rms(a) < 1e-10 or rms(b) < 1e-10:
        raise ValueError('Signal too quiet for automatic alignment; set offset_seconds explicitly')
    scores = correlate(b, a, method='fft')
    lags = correlation_lags(len(b), len(a))
    allowed = np.abs(lags * block / rate) <= max_lag
    coarse = int(lags[allowed][np.argmax(scores[allowed])]) * block
    # Refine only within one 5 ms bin, using an early 100 ms audible window.
    audible = np.flatnonzero(np.abs(reference) > np.max(np.abs(reference)) * .1)
    if not len(audible):
        return coarse
    start = int(audible[0])
    length = min(round(rate * .1), len(reference) - start)
    left, right = start + coarse - block, start + coarse + block + length
    if length > 0 and left >= 0 and right <= len(recording):
        template = reference[start:start + length]
        template = template - np.mean(template)
        scores = correlate(recording[left:right], template, mode='valid', method='fft')
        coarse += int(np.argmax(scores)) - block
    return coarse


def compare(reference, recording, offset, rate):
    start = max(0, -offset)
    end = min(len(reference), len(recording) - offset)
    if end - start < rate // 20:
        raise ValueError('Less than 50 ms overlap after alignment')
    a, b = reference[start:end], recording[start + offset:end + offset]
    a = a - np.mean(a)
    b = b - np.mean(b)
    energy = float(np.dot(a, a))
    if energy < 1e-18:
        return None
    gain = float(np.dot(a, b) / energy)
    residual = b - gain * a
    denominator = math.sqrt(energy * float(np.dot(b, b)))
    return {'offset_seconds': offset / rate, 'overlap_seconds': len(a) / rate,
            'fitted_gain': gain, 'correlation': float(np.dot(a, b) / denominator) if denominator else 0.,
            'fitted_residual_dbfs': db(rms(residual)),
            'fitted_residual_relative_db': db(rms(residual) / rms(b)) if rms(b) else None}


def burst_timing(x, rate, events, offset):
    expected = np.array([e['seconds'] for e in events if e['gate']])
    if len(expected) != 8:
        raise ValueError('Burst fixture needs eight note-ons')
    power = uniform_filter1d(x * x, max(1, round(rate * .002)))
    actual = []
    for t in expected:
        start, end = round((t + offset - .08) * rate), round((t + offset + .08) * rate)
        if start < 0 or end > len(x):
            raise ValueError('Burst onset window is outside the recording')
        window = power[start:end]
        floor = float(np.median(window))
        peak = float(np.max(window))
        if peak <= max(floor * 10, 1e-16):
            raise ValueError('Burst lacks 10 dB onset contrast')
        crossings = np.flatnonzero(window > floor + .1 * (peak - floor))
        actual.append((start + int(crossings[0])) / rate)
    slope, intercept = np.polyfit(expected - expected[0], actual, 1)
    error = np.array(actual) - (intercept + slope * (expected - expected[0]))
    return {'detected_onsets_seconds': actual, 'interval_slope_ppm': float((slope - 1) * 1e6),
            'maximum_timing_residual_ms': float(np.max(np.abs(error)) * 1000),
            'interpretation': 'MIDI/envelope/recording interval estimate; not a DAC clock measurement'}


def tone_frequency(x, rate, case, offset):
    on = next(e for e in case['events'] if e['gate'])
    off = next(e for e in case['events'] if not e['gate'])
    start = round((on['seconds'] + offset + .25) * rate)
    end = min(round((off['seconds'] + offset - .1) * rate), start + 2 * rate, len(x))
    if start < 0 or end - start < rate // 2:
        return None
    window = x[start:end] - np.mean(x[start:end])
    spectrum = np.abs(np.fft.rfft(window * np.hanning(len(window))))
    nominal = 440 * 2 ** ((on['note'] - 69) / 12)
    frequencies = np.fft.rfftfreq(len(window), 1 / rate)
    bins = np.flatnonzero((frequencies > nominal * .85) & (frequencies < nominal * 1.15))
    index = int(bins[np.argmax(spectrum[bins])])
    a, b, c = np.log(np.maximum(spectrum[index - 1:index + 2], 1e-300))
    correction = float(.5 * (a - c) / (a - 2 * b + c)) if a - 2 * b + c else 0.
    frequency = (index + correction) * rate / len(window)
    return {'fundamental_hz': frequency, 'nominal_hz': nominal,
            'nominal_error_cents': 1200 * math.log2(frequency / nominal),
            'interpretation': 'Includes instrument tuning and recording clock; not isolated DAC error'}


def analyze(args):
    directory = args.directory.resolve()
    session = json.loads((directory / 'session.json').read_text())
    if session.get('schema') != 1 or not session.get('cases'):
        raise ValueError('Unsupported or empty session')
    missing_metadata = [field for field in CHAIN_FIELDS if not session.get('chain', {}).get(field)
                        or 'confirm before capture' in str(session['chain'][field])]
    report = {'schema': 1, 'status': 'pending_recordings', 'hardware_fitted': False,
              'session_sha256': sha(directory / 'session.json'), 'chain': session.get('chain'),
              'analysis_versions': {'numpy': np.__version__, 'scipy': scipy.__version__},
              'missing_metadata': missing_metadata, 'cases': [],
              'limitations': [
                  'No model parameters are fitted or written back to the plugin.',
                  'Gain and phase are fitted per take for diagnostics; these residuals cannot rank DAC laws.',
                  'Reference frontend, filter and hardware timing errors remain mixed with converter error.',
                  'Automatic offsets can differ by cycles on periodic tones; they are not measured latency.',
                  'Near-full-scale checks cannot detect analog clipping below digital full scale.',
                  'Fit/holdout assignments are declared in advance, but no independent validation has occurred.']}
    present = 0
    for case in session['cases']:
        result = {'name': case['name'], 'partition': case['partition'], 'status': 'missing', 'warnings': []}
        path = directory / case['recording']
        if path.is_file():
            present += 1
            try:
                for filename, expected in case['input_sha256'].items():
                    if sha(directory / filename) != expected:
                        raise ValueError('Input changed since reference generation: ' + filename)
                channel = case.get('channel')
                if channel is None:
                    channel = session.get('recording_channel')
                rate, recording = load_audio(path, channel)
                result.update(status='measured', recording_sha256=sha(path), sample_rate=rate,
                              duration_seconds=len(recording) / rate, peak=float(np.max(np.abs(recording))),
                              rms_dbfs=db(rms(recording)), dc=float(np.mean(recording)),
                              near_full_scale_samples=int(np.count_nonzero(np.abs(recording) >= .9999)))
                if rate != session['expected_sample_rate']:
                    result['warnings'].append('Recording sample rate differs from the planned session rate')
                if result['near_full_scale_samples']:
                    result['warnings'].append('Near-full-scale samples: check clipping before fitting')
                result['comparisons'] = {}
                native_offset = None
                for model, reference_info in case['references'].items():
                    reference_path = directory / reference_info['path']
                    if sha(reference_path) != reference_info['sha256']:
                        raise ValueError('Reference changed since preparation: ' + model)
                    ref_rate, reference = load_audio(reference_path)
                    reference = resample(reference, ref_rate, rate)
                    if case['name'] == 'a-silence' or rms(reference) < 1e-12:
                        result['silence_ac_rms_dbfs'] = db(rms(recording - np.mean(recording)))
                        continue
                    offset = case.get('offset_seconds')
                    if offset is not None:
                        if not isinstance(offset, (float, int)) or not math.isfinite(offset):
                            raise ValueError('offset_seconds must be finite or null')
                        offset = round(offset * rate)
                        method = 'manual'
                    else:
                        offset = auto_offset(reference, recording, rate, args.max_lag)
                        method = 'automatic: envelope then local phase fit'
                    result['comparisons'][model] = compare(reference, recording, offset, rate)
                    if result['comparisons'][model] is not None:
                        result['comparisons'][model]['alignment'] = method
                    if model == 'native':
                        native_offset = offset / rate
                        comparison = result['comparisons'][model]
                        if comparison is not None and comparison['correlation'] < .25:
                            result['warnings'].append('Low native-reference correlation: review tuning, alignment and waveform before fitting')
                        if offset > 0 or case['events'][0]['seconds'] + native_offset > .2:
                            stop = min(len(recording), round((case['events'][0]['seconds'] + native_offset - .1) * rate))
                            if stop >= rate // 10:
                                baseline = recording[:stop]
                                result['pre_note_ac_rms_dbfs'] = db(rms(baseline - np.mean(baseline)))
                        expected_end = len(reference) + offset
                        if expected_end > len(recording) + rate * .05:
                            result['warnings'].append('Capture is shorter than the aligned reference; release tail is incomplete')
                if native_offset is not None and case['name'].startswith(('b-', 'e-')):
                    result['pitch'] = tone_frequency(recording, rate, case, native_offset)
                if native_offset is not None and case['name'] == 'c-bursts':
                    result['burst_timing'] = burst_timing(recording, rate, case['events'], native_offset)
            except (ValueError, OSError, KeyError) as error:
                result.update(status='error', error=str(error))
        report['cases'].append(result)
    report['recordings_present'] = present
    report['recordings_expected'] = len(session['cases'])
    if present:
        report['status'] = 'partial' if present < len(session['cases']) else 'measurements_for_review'
        if missing_metadata or any(c['status'] == 'error' or c['warnings'] for c in report['cases']):
            report['status'] = 'needs_attention'
    write_json(directory / 'report.json', report)
    lines = ['# CZ capture measurements', '', f"Status: **{report['status']}**. Hardware fitted: **no**.", '',
             f"Recordings: {present}/{len(session['cases'])}. Missing chain metadata: {', '.join(missing_metadata) or 'none'}.", '',
             '| Fixture | Partition | State | Peak | RMS dBFS |', '|---|---|---|---:|---:|']
    for c in report['cases']:
        peak = f"{c['peak']:.6f}" if 'peak' in c else '—'
        level = f"{c['rms_dbfs']:.2f}" if c.get('rms_dbfs') is not None else '—'
        lines.append(f"| {c['name']} | {c['partition']} | {c['status']} | {peak} | {level} |")
    lines += ['', '## Limits', ''] + [f'- {item}' for item in report['limitations']]
    for c in report['cases']:
        issues = c['warnings'] + ([c['error']] if c['status'] == 'error' else [])
        if issues:
            lines += ['', f"## {c['name']}", ''] + [f'- {item}' for item in issues]
    lines += ['', 'Detailed offsets, fitted residuals, hashes, pitch and burst timing are in `report.json`.', '']
    (directory / 'report.md').write_text('\n'.join(lines))
    print(f"{report['status']}: {present}/{len(session['cases'])} recordings; {directory / 'report.md'}")
    return 0 if report['status'] == 'measurements_for_review' else 2


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    prep = commands.add_parser('prepare', help='Unpack inputs and render exact MIDI-sequence references')
    prep.add_argument('directory', type=Path)
    prep.add_argument('--set', choices=('quick', 'full'), default='quick')
    prep.add_argument('--only', nargs='+', help='Exact fixture names, overriding --set')
    prep.add_argument('--rate', type=int, choices=(44100, 48000, 88200, 96000, 192000), default=96000)
    prep.add_argument('--reconstruction', choices=('hold', 'clean'), default='hold')
    prep.add_argument('--renderer', type=Path, default=ROOT / 'tests/render')
    analysis = commands.add_parser('analyze', help='Write an honest partial report from available recordings')
    analysis.add_argument('directory', type=Path)
    analysis.add_argument('--max-lag', type=float, default=5., help='Automatic alignment search, seconds')
    args = parser.parse_args()
    try:
        if args.command == 'prepare':
            prepare(args)
            return 0
        if not math.isfinite(args.max_lag) or not 0 < args.max_lag <= 30:
            raise ValueError('--max-lag must be between 0 and 30 seconds')
        return analyze(args)
    except (ValueError, OSError, subprocess.CalledProcessError, json.JSONDecodeError) as error:
        parser.exit(1, f'Error: {error}\n')


if __name__ == '__main__':
    raise SystemExit(main())
