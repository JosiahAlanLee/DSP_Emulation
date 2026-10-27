#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Known-signal checks for the recording analysis; not keyboard measurements."""
import argparse
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
import wave
import zipfile

import numpy as np
from scipy.io import wavfile

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('calibrate', ROOT / 'tools/calibrate.py')
cal = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cal)


class Measurements(unittest.TestCase):
    def test_alignment_gain_and_dc(self):
        rate = 48000
        rng = np.random.default_rng(933)
        reference = np.zeros(rate * 5)
        reference[rate:rate * 3] = rng.normal(0, .1, rate * 2)
        delay = 3127
        recording = np.r_[np.zeros(delay), reference * .37] + .02
        offset = cal.auto_offset(reference, recording, rate, 2)
        self.assertEqual(offset, delay)
        result = cal.compare(reference, recording, offset, rate)
        self.assertAlmostEqual(result['fitted_gain'], .37, places=10)
        self.assertAlmostEqual(result['correlation'], 1., places=10)
        self.assertLess(result['fitted_residual_dbfs'], -200)
        # Negative offsets are supported when a recorder started late.
        result = cal.compare(reference, reference[123:], -123, rate)
        self.assertAlmostEqual(result['correlation'], 1.)

    def test_silent_and_invalid_alignment(self):
        with self.assertRaises(ValueError):
            cal.auto_offset(np.zeros(48000), np.zeros(48000), 48000, 2)
        with self.assertRaises(ValueError):
            cal.compare(np.ones(200), np.ones(200), 100, 48000)

    def test_burst_interval_slope_and_jitter(self):
        rate = 96000
        events = [{'seconds': 2 + i * .51, 'gate': 1} for i in range(8)]
        x = np.zeros(rate * 7)
        for event in events:
            t = event['seconds'] * 1.0004 + .03
            start = round(t * rate)
            x[start:start + 960] = np.sin(2 * np.pi * 997 * np.arange(960) / rate) * .2
        result = cal.burst_timing(x, rate, events, .03)
        self.assertAlmostEqual(result['interval_slope_ppm'], 400, delta=5)
        self.assertLess(result['maximum_timing_residual_ms'], .02)

    def test_pitch(self):
        rate, hz = 48000, 263.13
        x = .1 * np.sin(2 * np.pi * hz * np.arange(rate * 5) / rate)
        case = {'events': [{'seconds': 1, 'note': 60, 'gate': 1},
                           {'seconds': 4, 'note': 60, 'gate': 0}]}
        result = cal.tone_frequency(x, rate, case, 0)
        self.assertAlmostEqual(result['fundamental_hz'], hz, delta=.012)
        self.assertGreater(result['nominal_error_cents'], 9)

    def test_wav_formats_and_channels(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / 'audio.wav'
            # Packed 24-bit PCM is returned left-justified by scipy.
            values = [-8388608, -4194304, 0, 4194304, 8388607]
            with wave.open(str(path), 'wb') as out:
                out.setparams((1, 3, 96000, 0, 'NONE', 'not compressed'))
                out.writeframes(b''.join((value & 0xffffff).to_bytes(3, 'little') for value in values))
            rate, x = cal.load_audio(path)
            self.assertEqual(rate, 96000)
            np.testing.assert_allclose(x, np.array(values) / 8388608)
            wavfile.write(path, 48000, np.array([[.1, .9], [.2, .8]], dtype=np.float32))
            with self.assertRaises(ValueError):
                cal.load_audio(path)
            _, x = cal.load_audio(path, 1)
            np.testing.assert_allclose(x, [.9, .8])
            wavfile.write(path, 48000, np.array([0, np.nan], dtype=np.float32))
            with self.assertRaises(ValueError):
                cal.load_audio(path)


class CaptureIntegration(unittest.TestCase):
    def test_every_fixture_keeps_its_notes_and_timing(self):
        with zipfile.ZipFile(cal.ARCHIVE) as archive:
            names = [name for name in archive.namelist() if name.endswith('.mid')]
            self.assertEqual(len(names), 98)
            for name in names:
                source = archive.read(name)
                original, duration, _ = cal.fixture_midi(source)
                padded, events, padded_duration = cal.padded_midi(source)
                self.assertEqual(len(original), len(events), name)
                self.assertAlmostEqual(padded_duration, duration + 3, msg=name)
                for a, b in zip(original, events):
                    self.assertAlmostEqual(b['seconds'] - a['seconds'], 2, msg=name)
                    self.assertEqual((a['slot'], a['note'], a['gate']),
                                     (b['slot'], b['note'], b['gate']), name)
                self.assertEqual(cal.fixture_midi(padded)[0], events, name)

    def test_padded_midi_and_real_reference_rendering(self):
        with tempfile.TemporaryDirectory() as temporary:
            session_dir = Path(temporary) / 'session'
            args = argparse.Namespace(directory=session_dir, renderer=ROOT / 'tests/render',
                                      only=['b-cosine-level-50', 'c-bursts', 'd-poly-8', 'a-silence'],
                                      set='quick', rate=48000)
            with contextlib.redirect_stdout(io.StringIO()):
                cal.prepare(args)
            manifest = json.loads((session_dir / 'session.json').read_text())
            self.assertEqual(len(manifest['cases']), 4)
            for case in manifest['cases']:
                name = case['name']
                events, duration, _ = cal.fixture_midi((session_dir / 'inputs' / (name + '.mid')).read_bytes())
                self.assertEqual(events, case['events'])
                self.assertAlmostEqual(duration, case['duration_seconds'])
                self.assertEqual(events[0]['seconds'], 2.)
                if name == 'c-bursts':
                    ons = [e for e in events if e['gate']]
                    offs = [e for e in events if not e['gate']]
                    self.assertEqual(len(ons), 8)
                    self.assertAlmostEqual(ons[1]['seconds'] - ons[0]['seconds'], (960 + 19) / 1920)
                    self.assertAlmostEqual(offs[0]['seconds'] - ons[0]['seconds'], 19 / 1920)
                if name == 'd-poly-8':
                    self.assertEqual(len({e['slot'] for e in events}), 8)
                    self.assertEqual(case['partition'], 'holdout')
                for model in cal.MODELS:
                    rate, x = cal.load_audio(session_dir / case['references'][model]['path'])
                    self.assertEqual(rate, 40000 if model == 'native' else 48000)
                    self.assertEqual(len(x), round(duration * rate))
                    self.assertLess(np.max(np.abs(x[:round(rate * 1.9)])), 1e-12)
                    if name != 'a-silence':
                        self.assertGreater(np.max(np.abs(x)), .00001)
            # No recordings: a report must say pending and must not call the model fitted.
            analysis = argparse.Namespace(directory=session_dir, max_lag=2.)
            with contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(cal.analyze(analysis), 2)
            report = json.loads((session_dir / 'report.json').read_text())
            self.assertEqual(report['status'], 'pending_recordings')
            self.assertFalse(report['hardware_fitted'])
            manifest['chain'] = {field: 'synthetic test only' for field in cal.CHAIN_FIELDS}
            for case in manifest['cases']:
                rate, x = cal.load_audio(session_dir / case['references']['instant']['path'])
                wavfile.write(session_dir / case['recording'], rate, np.r_[np.zeros(2400), x * .5].astype(np.float32))
            cal.write_json(session_dir / 'session.json', manifest)
            with contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(cal.analyze(analysis), 0)
            report = json.loads((session_dir / 'report.json').read_text())
            self.assertEqual(report['status'], 'measurements_for_review')
            for case in report['cases']:
                self.assertEqual(case['status'], 'measured')
                if case['name'] == 'a-silence':
                    self.assertIsNone(case['silence_ac_rms_dbfs'])
                else:
                    result = case['comparisons']['instant']
                    self.assertAlmostEqual(result['fitted_gain'], .5, delta=.0001, msg=case['name'])
                    if result['fitted_residual_dbfs'] is not None:  # null denotes an exact zero residual
                        self.assertLess(result['fitted_residual_dbfs'], -100, case['name'])
            # A capture at full scale needs attention, never a clean calibration verdict.
            case = manifest['cases'][1]
            path = session_dir / case['recording']
            rate, x = cal.load_audio(path)
            x[0] = 1.
            wavfile.write(path, rate, x.astype(np.float32))
            with contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(cal.analyze(analysis), 2)
            report = json.loads((session_dir / 'report.json').read_text())
            self.assertEqual(report['status'], 'needs_attention')
            self.assertEqual(report['cases'][1]['near_full_scale_samples'], 1)
            # Inputs cannot be quietly changed after the reference files were made.
            input_path = session_dir / next(iter(case['input_sha256']))
            input_path.write_bytes(input_path.read_bytes() + b'changed')
            with contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(cal.analyze(analysis), 2)
            report = json.loads((session_dir / 'report.json').read_text())
            self.assertEqual(report['cases'][1]['status'], 'error')
            with self.assertRaises(ValueError):
                cal.prepare(args)

    def test_renderer_rejects_bad_schedule(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            schedule = directory / 'bad.tsv'
            schedule.write_text('CZSEQ1 40000 1000\n0 0 60 1\n')  # missing note-off
            result = subprocess.run([str(ROOT / 'tests/render'), '--events',
                                     str(ROOT / 'examples/original-detuned-chime.syx'),
                                     str(schedule), str(directory / 'out.wav'), 'native'],
                                    capture_output=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertFalse((directory / 'out.wav').exists())
            schedule.write_text('CZSEQ1 40000 1000\n0 0 60 1\n500 0 60 0\n500 0 62 1\n')
            result = subprocess.run([str(ROOT / 'tests/render'), '--events',
                                     str(ROOT / 'examples/original-detuned-chime.syx'),
                                     str(schedule), str(directory / 'out.wav'), 'native'],
                                    capture_output=True)
            self.assertNotEqual(result.returncode, 0)


if __name__ == '__main__':
    unittest.main()
