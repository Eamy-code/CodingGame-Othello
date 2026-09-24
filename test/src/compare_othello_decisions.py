import argparse
import json
import queue
import subprocess
import sys
import threading
from pathlib import Path

FIRST_TURN_TIMEOUT_SECONDS = 2.0
TURN_TIMEOUT_SECONDS = 0.15


class BotSession:
    def __init__(self, executable: Path, player: str) -> None:
        command = [sys.executable, str(executable)] if executable.suffix.lower() == ".py" else [str(executable)]
        self.process = subprocess.Popen(
            command,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            text=True,
            encoding="utf-8",
            errors="replace",
            bufsize=1,
            cwd=str(executable.parent),
        )
        self.output_lines: queue.Queue[str | None] = queue.Queue()
        self.expert_mode = False
        self.request_count = 0
        self.reader = threading.Thread(target=self._read_output, daemon=True)
        self.reader.start()
        self._write(f"{player}\n8\n")

    def _read_output(self) -> None:
        assert self.process.stdout is not None
        for line in self.process.stdout:
            self.output_lines.put(line.rstrip("\r\n"))
        self.output_lines.put(None)

    def _write(self, value: str) -> None:
        if self.process.stdin is None:
            raise BrokenPipeError("AI input is unavailable")
        self.process.stdin.write(value)
        self.process.stdin.flush()

    def choose(
        self, turn: dict[str, object]
    ) -> tuple[str | None, str | None, bool]:
        input_lines = list(turn["board_before"])
        if self.expert_mode:
            history = turn.get("opponent_history", [])
            input_lines.append("".join(f"{action};" for action in history))
        legal_actions = list(turn["legal_actions"])
        input_lines.append(str(len(legal_actions)))
        input_lines.extend(legal_actions)
        timeout = FIRST_TURN_TIMEOUT_SECONDS if self.request_count == 0 else TURN_TIMEOUT_SECONDS

        try:
            self._write("\n".join(input_lines) + "\n")
            output_line = self.output_lines.get(timeout=timeout)
        except queue.Empty:
            return None, "TIMEOUT", self.expert_mode
        except (BrokenPipeError, OSError):
            return None, "PROCESS_ERROR", self.expert_mode

        if output_line is None:
            return None, "PROCESS_EXITED", self.expert_mode
        self.request_count += 1
        tokens = output_line.strip().split()
        if not tokens:
            return None, "EMPTY_OUTPUT", self.expert_mode
        if tokens[0] == "EXPERT":
            if len(tokens) < 2:
                return None, "INVALID_OUTPUT", self.expert_mode
            self.expert_mode = True
            action = tokens[1].lower()
        else:
            action = tokens[0].lower()
        if action not in legal_actions:
            return action, "ILLEGAL_MOVE", self.expert_mode
        return action, None, self.expert_mode

    def close(self) -> None:
        if self.process.poll() is None:
            self.process.terminate()
            try:
                self.process.wait(timeout=1.0)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait(timeout=1.0)
        self.reader.join(timeout=0.2)


def find_color_difference(
    old_executable: Path,
    new_executable: Path,
    turns: list[dict[str, object]],
    color: str,
) -> dict[str, object] | None:
    player_id = "0" if color == "Black" else "1"
    sessions = {
        "OLD": BotSession(old_executable, player_id),
        "NEW": BotSession(new_executable, player_id),
    }
    try:
        for turn in turns:
            if turn.get("automatic_pass") or turn.get("color") != color:
                continue
            old_action, old_error, old_expert_mode = sessions["OLD"].choose(turn)
            new_action, new_error, new_expert_mode = sessions["NEW"].choose(turn)
            if (
                old_action != new_action
                or old_error != new_error
                or old_expert_mode != new_expert_mode
            ):
                return {
                    "turn": turn["turn"],
                    "move_number": turn["move_number"],
                    "color": color,
                    "board": turn["board_before"],
                    "legal_actions": turn["legal_actions"],
                    "opponent_history": turn.get("opponent_history", []),
                    "old_action": old_action,
                    "old_error": old_error,
                    "old_expert_mode": old_expert_mode,
                    "new_action": new_action,
                    "new_error": new_error,
                    "new_expert_mode": new_expert_mode,
                }
            if old_error or new_error:
                return {
                    "turn": turn["turn"],
                    "move_number": turn["move_number"],
                    "color": color,
                    "board": turn["board_before"],
                    "legal_actions": turn["legal_actions"],
                    "opponent_history": turn.get("opponent_history", []),
                    "old_action": old_action,
                    "old_error": old_error,
                    "old_expert_mode": old_expert_mode,
                    "new_action": new_action,
                    "new_error": new_error,
                    "new_expert_mode": new_expert_mode,
                }
    finally:
        for session in sessions.values():
            session.close()
    return None


def print_difference(difference: dict[str, object]) -> None:
    print(
        f"First differing choice: turn={difference['turn']}, "
        f"move={difference['move_number']}, color={difference['color']}"
    )
    print(f"OLD: {difference['old_action']} ({difference['old_error'] or 'OK'})")
    print(f"NEW: {difference['new_action']} ({difference['new_error'] or 'OK'})")
    if difference["old_expert_mode"] != difference["new_expert_mode"]:
        print(
            "EXPERT mode: "
            f"OLD={difference['old_expert_mode']}, NEW={difference['new_expert_mode']}"
        )
    print(f"Legal actions: {', '.join(difference['legal_actions'])}")
    print("Board:")
    print("  a b c d e f g h")
    for row_number, row in enumerate(difference["board"], start=1):
        print(f"{row_number} " + " ".join(row))
    history = difference.get("opponent_history", [])
    if history:
        print(f"Opponent history: {';'.join(history)}")


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Compare two Othello bots on the same recorded positions."
    )
    parser.add_argument("--old", required=True, type=Path, help="OLD bot executable or Python file")
    parser.add_argument("--new", required=True, type=Path, help="NEW bot executable or Python file")
    parser.add_argument("--trace", required=True, type=Path, help="Match *_trace.json file")
    parser.add_argument("--game", required=True, type=int, help="1-based game number in the trace")
    return parser.parse_args()


def main() -> int:
    arguments = parse_arguments()
    trace = json.loads(arguments.trace.read_text(encoding="utf-8"))
    games = trace.get("games", [])
    if arguments.game < 1 or arguments.game > len(games):
        raise ValueError("--game is outside the range of games in the trace")

    game = games[arguments.game - 1]
    turns = game.get("turns", [])
    differences = [
        difference
        for color in ("Black", "White")
        if (difference := find_color_difference(
            arguments.old.resolve(), arguments.new.resolve(), turns, color
        )) is not None
    ]
    difference = min(differences, key=lambda item: int(item["turn"])) if differences else None
    if difference is None:
        print("The bots selected the same legal actions on all replayed positions.")
        return 0
    print_difference(difference)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
