# Séance change history

## 2.1.1

- Claim the spectral frame before an expander modifies it. A timed-out core waits until an active operation releases ownership, preventing concurrent spectrum reads and writes.
- Reject non-finite A and B audio before FFT and dry-path storage. Ignore non-finite CV and wrap extreme seed CV before integer conversion.
- Cancel a pending frame-size change when SIZE returns to the current size before the frame boundary.

## 2.1.0

Added Bridge, the spectral activity and byte-mask expander.
