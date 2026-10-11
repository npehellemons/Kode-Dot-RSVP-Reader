#!/usr/bin/env python3
"""Compare simulator-derived UI state against the core ReaderViewModel trace."""

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
    trace = run_json([args.view_model_trace])
    frame_data = run_json([args.frame_dump, "--text", trace["fixture_text"]])
    return trace, frame_data


def frame_at(frame_data: dict[str, Any], index: int) -> dict[str, Any]:
    return frame_data["frames"][index]


def chapters(frame_data: dict[str, Any]) -> list[dict[str, Any]]:
    parsed = frame_data.get("chapters")
    if isinstance(parsed, list) and parsed:
        return parsed
    return [
        {
            "index": 0,
            "title": "Document",
            "first_frame_index": 0,
            "frame_count": len(frame_data["frames"]),
        }
    ]


def current_chapter(frame_data: dict[str, Any], state: SimulatorState) -> dict[str, Any]:
    all_chapters = chapters(frame_data)
    for chapter in all_chapters:
        first = chapter["first_frame_index"]
        past = first + chapter["frame_count"]
        if first <= state.index < past:
            return chapter
    return all_chapters[-1]


def duration_ms(frame_data: dict[str, Any], state: SimulatorState, index: int) -> int:
    frame = frame_at(frame_data, index)
    if isinstance(frame.get("duration_ms"), int):
        return (frame["duration_ms"] * 250 + state.wpm - 1) // state.wpm
    return (60000 + state.wpm - 1) // state.wpm


def remaining_ms(frame_data: dict[str, Any], state: SimulatorState) -> int:
    return sum(duration_ms(frame_data, state, index)
               for index in range(state.index, len(frame_data["frames"])))


def chapter_remaining_ms(frame_data: dict[str, Any], state: SimulatorState) -> int:
    chapter = current_chapter(frame_data, state)
    past = min(chapter["first_frame_index"] + chapter["frame_count"],
               len(frame_data["frames"]))
    return sum(duration_ms(frame_data, state, index)
               for index in range(state.index, past))


def ceil_ms_to_seconds(ms: int) -> int:
    return (ms + 999) // 1000


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


def frame_text_for_context(frame: dict[str, Any]) -> str:
    text = frame["text"]
    if frame.get("continuation_after") and text.endswith("-"):
        return text[:-1]
    return text


def context_words(frame_data: dict[str, Any], state: SimulatorState) -> list[dict[str, Any]]:
    start, end = context_bounds(frame_data, state)
    current_source = frame_at(frame_data, state.index)["source_token_index"]
    words: list[dict[str, Any]] = []
    last_source = None
    for index in range(start, end + 1):
        frame = frame_at(frame_data, index)
        text = frame_text_for_context(frame)
        source = frame["source_token_index"]
        if source == last_source and words:
            words[-1]["text"] += text
            continue
        last_source = source
        words.append({"text": text, "current": source == current_source})
    return words


def make_view_model(
    label: str,
    at_ms: int,
    handled: bool,
    state: SimulatorState,
    frame_data: dict[str, Any],
) -> dict[str, Any]:
    visual_mode = mode(state)
    frame = frame_at(frame_data, state.index)
    chapter = current_chapter(frame_data, state)
    all_chapters = chapters(frame_data)
    chapter_frame_index = max(0, state.index - chapter["first_frame_index"])
    chapter_frame_count = chapter["frame_count"]
    if chapter_frame_count <= 1:
        progress = 0 if chapter_frame_count == 0 else 100
    else:
        progress = (chapter_frame_index * 100) // (chapter_frame_count - 1)

    if visual_mode == "sleep":
        primary = "Screen sleeping"
    elif visual_mode == "title":
        primary = chapter.get("title") or "Document"
    elif visual_mode == "context":
        primary = "Sentence context"
    else:
        primary = frame["text"]

    return {
        "label": label,
        "at_ms": at_ms,
        "handled": handled,
        "mode": visual_mode,
        "primary_text": primary,
        "chapter_title": chapter.get("title") or "",
        "chapter_index": chapter.get("index", 0) + 1 if all_chapters else 0,
        "chapter_count": len(all_chapters),
        "chapter_position": chapter_frame_index + 1 if chapter_frame_count else 0,
        "chapter_total": chapter_frame_count,
        "frame_position": state.index + 1 if frame_data["frames"] else 0,
        "frame_total": len(frame_data["frames"]),
        "progress_percent": max(0, min(100, progress)),
        "orp_codepoint_index": frame["orp_codepoint_index"],
        "font_scale_percent": frame.get("font_scale_percent", 100),
        "wpm": state.wpm,
        "remaining_seconds": ceil_ms_to_seconds(remaining_ms(frame_data, state)),
        "chapter_remaining_seconds": ceil_ms_to_seconds(
            chapter_remaining_ms(frame_data, state)
        ),
        "playing": state.playback_state == "playing",
        "paused": state.playback_state == "paused",
        "finished": state.playback_state == "finished",
        "context_words": context_words(frame_data, state)
        if visual_mode == "context"
        else [],
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
        state.playing = not state.playing
        state.playback_state = "playing" if state.playing else "paused"
        if state.playing:
            state.next_due_ms = at_ms + duration_ms(frame_data, state, state.index)
        return True
    if input_name == "dpad_right":
        step_word(frame_data, state, "right")
        state.title_card = False
        state.context_view = False
        return True
    if input_name == "hold_left":
        step_word(frame_data, state, "left")
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
    state.next_due_ms = at_ms + duration_ms(frame_data, state, state.index)
    return True


INPUT_BY_LABEL = {
    "red-starts": "red",
    "dpad-right-pauses": "dpad_right",
    "hold-left-context": "hold_left",
    "hold-end-rsvp": "hold_end",
    "sleep": "sleep",
    "red-wake-title": "red",
}


def replay_trace(trace: dict[str, Any], frame_data: dict[str, Any]) -> list[str]:
    failures: list[str] = []
    comparable_keys = [
        "handled",
        "mode",
        "primary_text",
        "chapter_title",
        "chapter_index",
        "chapter_count",
        "chapter_position",
        "chapter_total",
        "frame_position",
        "frame_total",
        "progress_percent",
        "orp_codepoint_index",
        "font_scale_percent",
        "wpm",
        "remaining_seconds",
        "chapter_remaining_seconds",
        "playing",
        "paused",
        "finished",
        "context_words",
    ]

    for scenario in trace["traces"]:
        state = SimulatorState()
        for expected in scenario["events"]:
            label = expected["label"]
            at_ms = expected["at_ms"]
            if label == "initial":
                handled = True
            elif label.startswith("tick"):
                handled = handle_tick(at_ms, state, frame_data)
            else:
                handled = handle_input(INPUT_BY_LABEL[label], at_ms, state, frame_data)

            actual = make_view_model(label, at_ms, handled, state, frame_data)
            for key in comparable_keys:
                if actual[key] != expected[key]:
                    failures.append(
                        f"{scenario['name']}:{label}:{key}: "
                        f"expected {expected[key]!r}, got {actual[key]!r}"
                    )
    return failures


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--view-model-trace", required=True)
    parser.add_argument("--frame-dump", required=True)
    args = parser.parse_args()

    trace, frame_data = load_inputs(args)
    failures = replay_trace(trace, frame_data)
    if failures:
        for failure in failures:
            print(f"FAIL: {failure}", file=sys.stderr)
        return 1
    print("Device simulator view-model trace check passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
