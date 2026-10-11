#!/usr/bin/env python3
"""Compare the device simulator input mirror against the core controller trace."""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from dataclasses import dataclass
from typing import Any


@dataclass
class SimulatorState:
    index: int = 0
    wpm: int = 250
    playback_state: str = "idle"
    playing: bool = False
    sleeping: bool = False
    title_card: bool = True
    context_view: bool = False
    next_due_ms: int = 0


def run_json(command: list[str], input_text: str | None = None) -> dict[str, Any]:
    result = subprocess.run(
        command,
        input=input_text,
        check=True,
        text=True,
        capture_output=True,
    )
    return json.loads(result.stdout)


def load_inputs(args: argparse.Namespace) -> tuple[dict[str, Any], dict[str, Any]]:
    trace = run_json([args.controller_trace])
    fixture_text = trace["fixture_text"]
    frame_data = run_json([args.frame_dump, "--text", fixture_text])
    return trace, frame_data


def frame_at(frame_data: dict[str, Any], index: int) -> dict[str, Any]:
    return frame_data["frames"][index]


def duration_ms(frame_data: dict[str, Any], state: SimulatorState) -> int:
    frame = frame_at(frame_data, state.index)
    if isinstance(frame.get("duration_ms"), int):
        return (frame["duration_ms"] * 250 + state.wpm - 1) // state.wpm
    return (60000 + state.wpm - 1) // state.wpm


def mode(state: SimulatorState) -> str:
    if state.sleeping:
        return "sleep"
    if state.context_view:
        return "context"
    if state.title_card:
        return "title"
    return "rsvp"


def context_bounds(frame_data: dict[str, Any], state: SimulatorState) -> tuple[int, int]:
    frame = frame_at(frame_data, state.index)
    return frame["context_start_frame_index"], frame["context_end_frame_index"]


def snapshot(
    label: str,
    at_ms: int,
    handled: bool,
    state: SimulatorState,
    frame_data: dict[str, Any],
) -> dict[str, Any]:
    frame = frame_at(frame_data, state.index)
    context_start, context_end = context_bounds(frame_data, state)
    return {
        "label": label,
        "at_ms": at_ms,
        "handled": handled,
        "mode": mode(state),
        "playback_state": state.playback_state,
        "frame_index": state.index,
        "source_token_index": frame["source_token_index"],
        "wpm": state.wpm,
        "visible_text": frame["text"],
        "context_start_frame_index": context_start,
        "context_end_frame_index": context_end,
    }


def step_word(frame_data: dict[str, Any], state: SimulatorState, direction: str) -> None:
    frame = frame_at(frame_data, state.index)
    key = "previous_word_frame_index" if direction == "left" else "next_word_frame_index"
    state.index = max(0, min(len(frame_data["frames"]) - 1, frame[key]))
    state.playing = False
    state.playback_state = "paused"


def handle_input(
    input_name: str,
    at_ms: int,
    state: SimulatorState,
    frame_data: dict[str, Any],
) -> bool:
    controller = frame_data["controller"]

    if state.sleeping:
        if input_name == "red":
            state.sleeping = False
            state.title_card = True
            state.context_view = False
            state.playing = False
            state.playback_state = "paused"
            return True
        return False

    if input_name == "red":
        state.title_card = False
        state.context_view = False
        if state.playback_state == "finished":
            state.index = 0
            state.playing = True
            state.playback_state = "playing"
            state.next_due_ms = at_ms + duration_ms(frame_data, state)
            return True
        state.playing = not state.playing
        state.playback_state = "playing" if state.playing else "paused"
        if state.playing:
            state.next_due_ms = at_ms + duration_ms(frame_data, state)
        return True

    if input_name == "blue":
        return False

    if input_name == "dpad_left":
        step_word(frame_data, state, "left")
        state.title_card = False
        state.context_view = False
        return True

    if input_name == "dpad_right":
        step_word(frame_data, state, "right")
        state.title_card = False
        state.context_view = False
        return True

    if input_name == "dpad_up":
        state.wpm = min(controller["max_wpm"], state.wpm + controller["wpm_step"])
        return True

    if input_name == "dpad_down":
        state.wpm = max(controller["min_wpm"], state.wpm - controller["wpm_step"])
        return True

    if input_name == "hold_left":
        step_word(frame_data, state, "left")
        state.title_card = False
        state.context_view = True
        return True

    if input_name == "hold_right":
        step_word(frame_data, state, "right")
        state.title_card = False
        state.context_view = True
        return True

    if input_name == "hold_end":
        if state.context_view:
            state.context_view = False
            state.title_card = False
            return True
        return False

    if input_name == "sleep":
        state.playing = False
        state.playback_state = "paused"
        state.context_view = False
        state.sleeping = True
        return True

    raise ValueError(f"unsupported input: {input_name}")


def handle_tick(at_ms: int, state: SimulatorState, frame_data: dict[str, Any]) -> bool:
    if mode(state) != "rsvp" or not state.playing:
        return False
    if at_ms < state.next_due_ms:
        return False
    if state.index + 1 >= len(frame_data["frames"]):
        state.playing = False
        state.playback_state = "finished"
        return True
    state.index += 1
    state.next_due_ms = at_ms + duration_ms(frame_data, state)
    return True


INPUT_BY_LABEL = {
    "red-starts": "red",
    "red-pauses": "red",
    "red-resumes": "red",
    "red-pauses-again": "red",
    "red-wakes-title": "red",
    "sleep": "sleep",
    "right-pauses-next-word": "dpad_right",
    "right-pauses-next-word-again": "dpad_right",
    "left-pauses-previous-word": "dpad_left",
    "hold-right-enters-context": "hold_right",
    "hold-right-repeats-context": "hold_right",
    "hold-end-returns-rsvp": "hold_end",
    "wpm-up": "dpad_up",
    "wpm-up-again": "dpad_up",
    "wpm-down": "dpad_down",
    "wpm-down-again": "dpad_down",
    "wpm-down-third": "dpad_down",
}


def replay_trace(trace: dict[str, Any], frame_data: dict[str, Any]) -> list[str]:
    failures: list[str] = []
    comparable_keys = [
        "handled",
        "mode",
        "playback_state",
        "frame_index",
        "source_token_index",
        "wpm",
        "visible_text",
        "context_start_frame_index",
        "context_end_frame_index",
    ]

    for scenario in trace["traces"]:
        state = SimulatorState()
        for expected in scenario["events"]:
            label = expected["label"]
            at_ms = expected["at_ms"]
            if label == "initial":
                actual = snapshot(label, at_ms, True, state, frame_data)
            elif label.startswith("tick") or label.endswith("asleep"):
                handled = handle_tick(at_ms, state, frame_data)
                actual = snapshot(label, at_ms, handled, state, frame_data)
            else:
                input_name = INPUT_BY_LABEL[label]
                handled = handle_input(input_name, at_ms, state, frame_data)
                actual = snapshot(label, at_ms, handled, state, frame_data)

            for key in comparable_keys:
                if actual[key] != expected[key]:
                    failures.append(
                        f"{scenario['name']}:{label}:{key}: "
                        f"expected {expected[key]!r}, got {actual[key]!r}"
                    )
    return failures


def validate_controller_contract(frame_data: dict[str, Any], trace: dict[str, Any]) -> list[str]:
    failures: list[str] = []
    controller = frame_data.get("controller", {})
    if frame_data.get("schema") != "rsvp-display-frames-v1":
        failures.append("frame data schema is not rsvp-display-frames-v1")
    if controller.get("schema") != trace.get("controller_schema"):
        failures.append("controller schema does not match trace controller schema")
    if controller.get("red_sleeping") != "wake_to_title_without_advancing":
        failures.append("red_sleeping contract changed")
    if controller.get("wpm_step") != 25:
        failures.append("wpm_step contract changed")
    return failures


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--controller-trace", required=True)
    parser.add_argument("--frame-dump", required=True)
    args = parser.parse_args()

    trace, frame_data = load_inputs(args)
    failures = validate_controller_contract(frame_data, trace)
    failures.extend(replay_trace(trace, frame_data))

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}", file=sys.stderr)
        return 1

    print("Device simulator controller trace check passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
