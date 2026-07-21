import argparse
import queue
import subprocess
import sys
import threading
import time
from dataclasses import dataclass
from pathlib import Path


BOARD_SIZE = 8
FIRST_TURN_TIMEOUT_SECONDS = 2.0
TURN_TIMEOUT_SECONDS = 0.15
EMPTY = "."
BLACK = "0"
WHITE = "1"
DIRECTIONS = (
    (-1, -1),
    (-1, 0),
    (-1, 1),
    (0, -1),
    (0, 1),
    (1, -1),
    (1, 0),
    (1, 1),
)


def opponent_of(player: str) -> str:
    return WHITE if player == BLACK else BLACK


def color_name(player: str) -> str:
    return "Black" if player == BLACK else "White"


def action_to_position(action: str) -> tuple[int, int]:
    return int(action[1]) - 1, ord(action[0]) - ord("a")


def position_to_action(row: int, column: int) -> str:
    return f"{chr(ord('a') + column)}{row + 1}"


def is_inside(row: int, column: int) -> bool:
    return 0 <= row < BOARD_SIZE and 0 <= column < BOARD_SIZE


def flips_for_move(
    board: list[list[str]], action: str, player: str
) -> list[tuple[int, int]]:
    row, column = action_to_position(action)
    if not is_inside(row, column) or board[row][column] != EMPTY:
        return []

    opponent = opponent_of(player)
    all_flips: list[tuple[int, int]] = []
    for row_delta, column_delta in DIRECTIONS:
        current_row = row + row_delta
        current_column = column + column_delta
        direction_flips: list[tuple[int, int]] = []

        while (
            is_inside(current_row, current_column)
            and board[current_row][current_column] == opponent
        ):
            direction_flips.append((current_row, current_column))
            current_row += row_delta
            current_column += column_delta

        if (
            direction_flips
            and is_inside(current_row, current_column)
            and board[current_row][current_column] == player
        ):
            all_flips.extend(direction_flips)

    return all_flips


def legal_actions(board: list[list[str]], player: str) -> list[str]:
    actions: list[str] = []
    for row in range(BOARD_SIZE):
        for column in range(BOARD_SIZE):
            action = position_to_action(row, column)
            if flips_for_move(board, action, player):
                actions.append(action)
    return actions


def apply_move(board: list[list[str]], action: str, player: str) -> None:
    flips = flips_for_move(board, action, player)
    row, column = action_to_position(action)
    board[row][column] = player
    for flip_row, flip_column in flips:
        board[flip_row][flip_column] = player


def initial_board() -> list[list[str]]:
    rows = [list("........") for _ in range(BOARD_SIZE)]
    rows[3][3] = BLACK
    rows[3][4] = WHITE
    rows[4][3] = WHITE
    rows[4][4] = BLACK
    return rows


def count_stones(board: list[list[str]], player: str) -> int:
    return sum(row.count(player) for row in board)


class BotProcess:
    def __init__(self, executable: Path, player: str) -> None:
        self.player = player
        self.action_count = 0
        self.expert_mode = False
        self.output_lines: queue.Queue[str | None] = queue.Queue()
        command = (
            [sys.executable, str(executable)]
            if executable.suffix.lower() == ".py"
            else [str(executable)]
        )
        self.process = subprocess.Popen(
            command,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            encoding="utf-8",
            errors="replace",
            bufsize=1,
            cwd=str(executable.parent),
        )
        threading.Thread(target=self._read_stdout, daemon=True).start()
        threading.Thread(target=self._drain_stderr, daemon=True).start()
        self._write(f"{player}\n{BOARD_SIZE}\n")

    def _read_stdout(self) -> None:
        assert self.process.stdout is not None
        for line in self.process.stdout:
            self.output_lines.put(line.rstrip("\r\n"))
        self.output_lines.put(None)

    def _drain_stderr(self) -> None:
        assert self.process.stderr is not None
        for _ in self.process.stderr:
            pass

    def _write(self, value: str) -> None:
        if self.process.stdin is None:
            raise BrokenPipeError("Bot standard input is unavailable.")
        self.process.stdin.write(value)
        self.process.stdin.flush()

    def request_action(
        self,
        board: list[list[str]],
        actions: list[str],
        opponent_history: list[str],
    ) -> tuple[str | None, str | None]:
        input_lines = ["".join(row) for row in board]
        if self.expert_mode:
            input_lines.append("".join(f"{action};" for action in opponent_history))
        input_lines.append(str(len(actions)))
        input_lines.extend(actions)

        timeout = (
            FIRST_TURN_TIMEOUT_SECONDS
            if self.action_count == 0
            else TURN_TIMEOUT_SECONDS
        )
        start_time = time.perf_counter()
        try:
            self._write("\n".join(input_lines) + "\n")
            output_line = self.output_lines.get(timeout=timeout)
        except queue.Empty:
            return None, "TIMEOUT"
        except (BrokenPipeError, OSError):
            return None, "PROCESS_ERROR"

        elapsed = time.perf_counter() - start_time
        if elapsed > timeout:
            return None, "TIMEOUT"
        if output_line is None:
            return None, "PROCESS_EXITED"

        self.action_count += 1
        tokens = output_line.strip().split()
        if not tokens:
            return None, "EMPTY_OUTPUT"

        if tokens[0] == "EXPERT":
            if len(tokens) < 2:
                return None, "INVALID_OUTPUT"
            self.expert_mode = True
            action = tokens[1].lower()
        else:
            action = tokens[0].lower()

        if action == "pass":
            return None, "PASS_OUTPUT"
        if action not in actions:
            return None, "ILLEGAL_MOVE"
        return action, None

    def close(self) -> None:
        if self.process.poll() is None:
            self.process.terminate()
            try:
                self.process.wait(timeout=1.0)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait(timeout=1.0)


@dataclass
class GameResult:
    winner_label: str | None
    loser_label: str | None
    colors: dict[str, str]
    stones: dict[str, int]
    reason: str


def play_game(
    old_executable: Path,
    new_executable: Path,
    old_color: str,
) -> GameResult:
    new_color = opponent_of(old_color)
    colors = {"OLD": old_color, "NEW": new_color}
    labels_by_color = {old_color: "OLD", new_color: "NEW"}
    bots = {
        old_color: BotProcess(old_executable, old_color),
        new_color: BotProcess(new_executable, new_color),
    }
    pending_history: dict[str, list[str]] = {BLACK: [], WHITE: []}
    board = initial_board()
    current_player = BLACK
    forfeit_reason: str | None = None
    forfeiting_player: str | None = None

    try:
        while True:
            actions = legal_actions(board, current_player)
            opponent = opponent_of(current_player)

            if not actions:
                if not legal_actions(board, opponent):
                    break
                pending_history[opponent].append("pass")
                current_player = opponent
                continue

            history = pending_history[current_player]
            pending_history[current_player] = []
            action, error = bots[current_player].request_action(
                board,
                actions,
                history,
            )
            if error is not None or action is None:
                forfeit_reason = error or "INVALID_OUTPUT"
                forfeiting_player = current_player
                break

            apply_move(board, action, current_player)
            pending_history[opponent].append(action)
            current_player = opponent
    finally:
        for bot in bots.values():
            bot.close()

    stones = {
        "OLD": count_stones(board, old_color),
        "NEW": count_stones(board, new_color),
    }
    if forfeiting_player is not None:
        loser_label = labels_by_color[forfeiting_player]
        winner_label = labels_by_color[opponent_of(forfeiting_player)]
        return GameResult(
            winner_label,
            loser_label,
            colors,
            stones,
            forfeit_reason or "FORFEIT",
        )

    if stones["OLD"] == stones["NEW"]:
        return GameResult(None, None, colors, stones, "DRAW")

    winner_label = "OLD" if stones["OLD"] > stones["NEW"] else "NEW"
    loser_label = "NEW" if winner_label == "OLD" else "OLD"
    return GameResult(winner_label, loser_label, colors, stones, "STONE_COUNT")


def print_game_result(game_number: int, result: GameResult) -> None:
    print(f"Game {game_number}")
    if result.winner_label is None:
        print("Result: Draw")
        for label in ("OLD", "NEW"):
            print(
                f"{label}: Color={color_name(result.colors[label])}, "
                f"Stones={result.stones[label]}"
            )
    else:
        winner = result.winner_label
        loser = result.loser_label
        assert loser is not None
        print(
            f"Winner: {winner}, Color={color_name(result.colors[winner])}, "
            f"Stones={result.stones[winner]}"
        )
        print(
            f"Loser: {loser}, Color={color_name(result.colors[loser])}, "
            f"Stones={result.stones[loser]}"
        )
        if result.reason != "STONE_COUNT":
            print(f"Reason: {result.reason}")
    print()


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Run local Othello bot matches.")
    parser.add_argument("--old", required=True, type=Path)
    parser.add_argument("--new", required=True, type=Path)
    parser.add_argument("--new-change", required=True)
    parser.add_argument("--games", type=int, default=20)
    return parser.parse_args()


def main() -> int:
    arguments = parse_arguments()
    if arguments.games <= 0:
        raise ValueError("--games must be greater than zero.")

    old_executable = arguments.old.resolve()
    new_executable = arguments.new.resolve()

    wins = {"OLD": 0, "NEW": 0}
    total_stones = {"OLD": 0, "NEW": 0}
    draws = 0

    print("Match Information")
    print(f"NEW Change: {arguments.new_change}")
    print(f"Games: {arguments.games}")
    print()

    for game_index in range(arguments.games):
        old_color = BLACK if game_index % 2 == 0 else WHITE
        result = play_game(old_executable, new_executable, old_color)
        print_game_result(game_index + 1, result)

        if result.winner_label is None:
            draws += 1
        else:
            wins[result.winner_label] += 1
        total_stones["OLD"] += result.stones["OLD"]
        total_stones["NEW"] += result.stones["NEW"]

    print("Final Summary")
    print(f"NEW Change: {arguments.new_change}")
    print(
        f"OLD: Wins={wins['OLD']}, Total Stones={total_stones['OLD']}"
    )
    print(
        f"NEW: Wins={wins['NEW']}, Total Stones={total_stones['NEW']}"
    )
    print(f"Draws: {draws}")

    if wins["OLD"] != wins["NEW"]:
        better = "OLD" if wins["OLD"] > wins["NEW"] else "NEW"
        print(f"Overall Result: {better} is better (more wins).")
    elif total_stones["OLD"] != total_stones["NEW"]:
        better = "OLD" if total_stones["OLD"] > total_stones["NEW"] else "NEW"
        print(
            f"Overall Result: {better} is better "
            "(wins tied; higher total stone count)."
        )
    else:
        print("Overall Result: TIE.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
