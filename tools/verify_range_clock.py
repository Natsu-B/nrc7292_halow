#!/usr/bin/env python3
"""Check byte-matched source timestamps and bounded relative receive/PTS drift.

The ESP and PC clocks are not synchronized: a constant pre-existing camera delay
cannot be measured here. This detects accumulated delay, repeated timestamps,
slower playback, and whole source-cycle jumps after the first received anchor.
"""
import argparse
import json
import math
from pathlib import Path
import unittest

MASK = 0xffffffff
HALF = 0x80000000


def verify(esp_text, timeline_text, max_drift_ms=3000):
    if not math.isfinite(max_drift_ms) or not 0 < max_drift_ms <= 5000:
        raise ValueError('Clock drift tolerance must be 1..5000 ms')
    profiles = {}
    for line in esp_text.splitlines():
        try:
            event = json.loads(line)
        except ValueError:
            continue
        if event.get('event') == 'video_profile':
            stream = event['stream_id']
            if stream in profiles and profiles[stream] != event:
                raise ValueError('Conflicting profile records for one stream')
            profiles[stream] = event
    frames = [event for event in map(json.loads, timeline_text.splitlines())
              if event['event'] == 'frame_match']
    if len(frames) < 2 or not profiles:
        raise ValueError('Missing real frame/ESP profile evidence')
    anchor = frames[0]
    epochs, streams, drifts = set(), set(), []
    previous_elapsed = anchor['elapsedMs']
    for frame in frames:
        profile = profiles[frame['stream']]
        epochs.add(profile['capture_epoch_ms'])
        streams.add(frame['stream'])
        # PTS is uint32 on the wire; the ESP epoch in its serial log is uint64.
        source_elapsed = (frame['pts'] - (profile['capture_epoch_ms'] & MASK)) & MASK
        source_pts = source_elapsed % 12000
        if not any(ref['profile'] == profile['source'] and
                   ref['frameIndex'] * (100 if ref['profile'] == 0 else 200) == source_pts
                   for ref in frame['references']):
            raise ValueError('Wrong source frame or source timestamp for received bytes')
        received_delta = frame['elapsedMs'] - anchor['elapsedMs']
        if frame['elapsedMs'] < previous_elapsed or not 0 <= received_delta < HALF:
            raise ValueError('Invalid receive timeline order or duration')
        previous_elapsed = frame['elapsedMs']
        pts_delta = ((frame['pts'] - anchor['pts'] + HALF) & MASK) - HALF
        drift = received_delta - pts_delta
        drifts.append(drift)
        if abs(drift) > max_drift_ms:
            raise ValueError(f'Receive/PTS drift {drift} ms exceeds {max_drift_ms} ms')
    if len(epochs) != 1:
        raise ValueError('Capture clock restarted during quality changes')
    span = frames[-1]['elapsedMs'] - anchor['elapsedMs']
    if span < 2 * max_drift_ms:
        raise ValueError('Observation is too short to establish bounded source clock progress')
    return dict(result='PASS', matched_frames=len(frames), streams=len(streams),
                capture_epochs=len(epochs), receive_span_ms=span,
                min_relative_drift_ms=min(drifts), max_relative_drift_ms=max(drifts),
                allowed_relative_drift_ms=max_drift_ms,
                criterion='byte-matched source clock with bounded wrap-aware receive/PTS progress',
                limitation='Unsynchronized clocks: absolute camera age and constant pre-anchor delay are not proven')


class ClockTests(unittest.TestCase):
    def case(self, points, epoch=1000):
        profile=dict(event='video_profile', stream_id=1, source=4, capture_epoch_ms=epoch)
        frames=[dict(event='frame_match', stream=1, elapsedMs=elapsed, pts=pts & MASK,
                     references=[dict(profile=4, frameIndex=((pts-epoch) % 12000)//200)])
                for elapsed, pts in points]
        return json.dumps(profile), '\n'.join(map(json.dumps, frames))

    def test_normal_and_uint32_wrap(self):
        for epoch in (1000, MASK-199, MASK+1001):
            result=verify(*self.case([(1000,epoch),(11150,epoch+10000)],epoch))
            self.assertEqual(result['max_relative_drift_ms'],150)

    def test_same_frame_replay(self):
        with self.assertRaisesRegex(ValueError,'drift'):
            verify(*self.case([(1000,1000),(11000,1000)]))

    def test_slow_playback(self):
        with self.assertRaisesRegex(ValueError,'drift'):
            verify(*self.case([(1000,1000),(11000,3000)]))

    def test_whole_cycle_backlog_after_anchor(self):
        with self.assertRaisesRegex(ValueError,'drift'):
            verify(*self.case([(1000,13000),(11000,11000)]))

    def test_variable_transport_delay_within_budget(self):
        self.assertEqual(verify(*self.case([(1000,1000),(5200,3000),(11000,11000)]))['result'],'PASS')


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('log_dir',nargs='?',type=Path)
    parser.add_argument('--self-test',action='store_true')
    args=parser.parse_args()
    if args.self_test:
        result=unittest.TextTestRunner().run(unittest.defaultTestLoader.loadTestsFromTestCase(ClockTests))
        return 0 if result.wasSuccessful() else 1
    if args.log_dir is None:
        parser.error('log_dir is required')
    try:
        result=verify((args.log_dir/'esp.log').read_text(errors='replace'),
                      (args.log_dir/'range_timeline.jsonl').read_text())
    except (ValueError,KeyError,TypeError,OSError) as error:
        print(json.dumps(dict(result='FAIL',error=str(error))))
        return 1
    print(json.dumps(result))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
