import argparse
import queue
import re
import subprocess
import sys
import threading
import time
from dataclasses import dataclass, field
from datetime import datetime
from pathlib import Path

BOARD_SIZE = 8
FIRST_TURN_TIMEOUT_SECONDS = 2.0
TURN_TIMEOUT_SECONDS = 0.15
EMPTY = "."
BLACK = "0"
WHITE = "1"
DIRECTIONS = (
    (-1, -1), (-1, 0), (-1, 1),
    (0, -1),           (0, 1),
    (1, -1),  (1, 0),  (1, 1),
)
METRIC_PATTERN = re.compile(
    r"^OTHELLO_METRIC\s+turn=(\d+)\s+book=([01])\s+max_depth=(\d+)\s+perfect=([01])$"
)
DEPTH_PATTERN = re.compile(r"^depth=(\d+)\b")


def opponent_of(player: str) -> str:
    if player == BLACK:
        return WHITE
    return BLACK


def color_name(player: str) -> str:
    if player == BLACK:
        return "Black"
    return "White"


def action_to_position(action: str) -> tuple[int, int]:
    return int(action[1]) - 1, ord(action[0]) - ord("a")


def position_to_action(row: int, column: int) -> str:
    return f"{chr(ord('a') + column)}{row + 1}"


def is_inside(row: int, column: int) -> bool:
    return 0 <= row < BOARD_SIZE and 0 <= column < BOARD_SIZE


def flips_for_move(board: list[list[str]], action: str, player: str) -> list[tuple[int, int]]:
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


@dataclass
class TurnMetric:
    turn: int
    used_book: bool = False
    max_depth: int = 0
    perfect_completed: bool = False


@dataclass
class BotGameSummary:
    book_last_move: int | None = None
    max_depth: int = 0
    perfect_completed: bool = False
    metric_available: bool = False


class BotProcess:
    def __init__(self, executable: Path, player: str) -> None:
        self.player = player
        self.action_count = 0
        self.expert_mode = False
        self.output_lines: queue.Queue[str | None] = queue.Queue()
        self.metrics: dict[int, TurnMetric] = {}
        self.metrics_lock = threading.Lock()
        self.current_request_turn = 0

        if executable.suffix.lower() == ".py":
            command = [sys.executable, str(executable)]
        else:
            command = [str(executable)]

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
        self.stdout_thread = threading.Thread(target=self._read_stdout, daemon=True)
        self.stderr_thread = threading.Thread(target=self._read_stderr, daemon=True)
        self.stdout_thread.start()
        self.stderr_thread.start()
        self._write(f"{player}\n{BOARD_SIZE}\n")

    def _read_stdout(self) -> None:
        assert self.process.stdout is not None
        for line in self.process.stdout:
            self.output_lines.put(line.rstrip("\r\n"))
        self.output_lines.put(None)

    def _metric_for_turn(self, turn: int) -> TurnMetric:
        metric = self.metrics.get(turn)
        if metric is None:
            metric = TurnMetric(turn=turn)
            self.metrics[turn] = metric
        return metric

    def _read_stderr(self) -> None:
        assert self.process.stderr is not None
        for raw_line in self.process.stderr:
            line = raw_line.rstrip("\r\n")
            explicit_match = METRIC_PATTERN.match(line)
            with self.metrics_lock:
                if explicit_match is not None:
                    turn = int(explicit_match.group(1))
                    self.metrics[turn] = TurnMetric(
                        turn=turn,
                        used_book=explicit_match.group(2) == "1",
                        max_depth=int(explicit_match.group(3)),
                        perfect_completed=explicit_match.group(4) == "1",
                    )
                    continue

                # ver.1 / ver.2など既存デバッグログにも可能な範囲で対応します。
                turn = self.current_request_turn
                if turn <= 0:
                    continue
                metric = self._metric_for_turn(turn)
                if line.startswith("standard="):
                    metric.used_book = True
                depth_match = DEPTH_PATTERN.match(line)
                if depth_match is not None:
                    depth = int(depth_match.group(1))
                    if depth > metric.max_depth:
                        metric.max_depth = depth
                if line.startswith("perfect score="):
                    metric.perfect_completed = True

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
    ) -> tuple[str | None, str | None, int]:
        input_lines = ["".join(row) for row in board]
        if self.expert_mode:
            input_lines.append("".join(f"{action};" for action in opponent_history))
        input_lines.append(str(len(actions)))
        input_lines.extend(actions)

        request_turn = self.action_count + 1
        with self.metrics_lock:
            self.current_request_turn = request_turn

        if self.action_count == 0:
            timeout = FIRST_TURN_TIMEOUT_SECONDS
        else:
            timeout = TURN_TIMEOUT_SECONDS

        start_time = time.perf_counter()
        try:
            self._write("\n".join(input_lines) + "\n")
            output_line = self.output_lines.get(timeout=timeout)
        except queue.Empty:
            return None, "TIMEOUT", request_turn
        except (BrokenPipeError, OSError):
            return None, "PROCESS_ERROR", request_turn

        elapsed = time.perf_counter() - start_time
        if elapsed > timeout:
            return None, "TIMEOUT", request_turn
        if output_line is None:
            return None, "PROCESS_EXITED", request_turn

        self.action_count += 1
        tokens = output_line.strip().split()
        if not tokens:
            return None, "EMPTY_OUTPUT", request_turn

        if tokens[0] == "EXPERT":
            if len(tokens) < 2:
                return None, "INVALID_OUTPUT", request_turn
            self.expert_mode = True
            action = tokens[1].lower()
        else:
            action = tokens[0].lower()

        if action == "pass":
            return None, "PASS_OUTPUT", request_turn
        if action not in actions:
            return None, "ILLEGAL_MOVE", request_turn
        return action, None, request_turn

    def close(self) -> None:
        if self.process.poll() is None:
            self.process.terminate()
            try:
                self.process.wait(timeout=1.0)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait(timeout=1.0)
        self.stderr_thread.join(timeout=0.2)

    def summarize(self, move_numbers_by_turn: dict[int, int]) -> BotGameSummary:
        with self.metrics_lock:
            metrics = list(self.metrics.values())

        if not metrics:
            return BotGameSummary()

        summary = BotGameSummary(metric_available=True)
        for metric in metrics:
            if metric.max_depth > summary.max_depth:
                summary.max_depth = metric.max_depth
            if metric.perfect_completed:
                summary.perfect_completed = True
            if metric.used_book:
                move_number = move_numbers_by_turn.get(metric.turn)
                if move_number is not None:
                    if summary.book_last_move is None or move_number > summary.book_last_move:
                        summary.book_last_move = move_number
        return summary


@dataclass
class GameResult:
    winner_label: str | None
    loser_label: str | None
    colors: dict[str, str]
    stones: dict[str, int]
    reason: str
    analytics: dict[str, BotGameSummary] = field(default_factory=dict)


def play_game(old_executable: Path, new_executable: Path, old_color: str) -> GameResult:
    new_color = opponent_of(old_color)
    colors = {"OLD": old_color, "NEW": new_color}
    labels_by_color = {old_color: "OLD", new_color: "NEW"}
    bots = {
        old_color: BotProcess(old_executable, old_color),
        new_color: BotProcess(new_executable, new_color),
    }
    pending_history: dict[str, list[str]] = {BLACK: [], WHITE: []}
    move_numbers_by_label: dict[str, dict[int, int]] = {"OLD": {}, "NEW": {}}
    board = initial_board()
    current_player = BLACK
    placed_move_number = 0
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
            action, error, bot_turn = bots[current_player].request_action(board, actions, history)
            if error is not None or action is None:
                forfeit_reason = error or "INVALID_OUTPUT"
                forfeiting_player = current_player
                break

            placed_move_number += 1
            label = labels_by_color[current_player]
            move_numbers_by_label[label][bot_turn] = placed_move_number
            apply_move(board, action, current_player)
            pending_history[opponent].append(action)
            current_player = opponent
    finally:
        for bot in bots.values():
            bot.close()

    analytics = {
        label: bots[color].summarize(move_numbers_by_label[label])
        for label, color in colors.items()
    }
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
            analytics,
        )

    if stones["OLD"] == stones["NEW"]:
        return GameResult(None, None, colors, stones, "DRAW", analytics)

    if stones["OLD"] > stones["NEW"]:
        winner_label = "OLD"
    else:
        winner_label = "NEW"
    if winner_label == "OLD":
        loser_label = "NEW"
    else:
        loser_label = "OLD"
    return GameResult(winner_label, loser_label, colors, stones, "STONE_COUNT", analytics)


def metric_text(value: int | None, available: bool) -> str:
    if not available:
        return "N/A"
    if value is None:
        return "0（定石なし）"
    return str(value)


def bool_text(value: bool, available: bool) -> str:
    if not available:
        return "N/A"
    if value:
        return "T"
    return "F"


def sanitize_filename_component(value: str) -> str:
    sanitized = re.sub(r'[<>:"/\\|?*]', "_", value.strip())
    sanitized = re.sub(r"\s+", "_", sanitized)
    sanitized = sanitized.rstrip(". ")
    if not sanitized:
        return "change"
    return sanitized


def print_game_result(game_number: int, result: GameResult) -> None:
    print(f"Game {game_number}")
    if result.winner_label is None:
        print("Result: Draw")
    else:
        print(f"Winner: {result.winner_label}")
    for label in ("OLD", "NEW"):
        summary = result.analytics[label]
        print(
            f"{label}: Color={color_name(result.colors[label])}, "
            f"Stones={result.stones[label]}, "
            f"BookLastMove={metric_text(summary.book_last_move, summary.metric_available)}, "
            f"MaxDepth={summary.max_depth if summary.metric_available else 'N/A'}, "
            f"Perfect={bool_text(summary.perfect_completed, summary.metric_available)}"
        )
    if result.reason not in ("STONE_COUNT", "DRAW"):
        print(f"Reason: {result.reason}")
    print()


def create_markdown_report(
    results: list[GameResult],
    arguments: argparse.Namespace,
    wins: dict[str, int],
    total_stones: dict[str, int],
    draws: int,
) -> str:
    generated_at = datetime.now().astimezone()
    lines = [
        "# Othello Local Match Result",
        "",
        "## Match Information",
        "",
        f"- 実行日時: {generated_at.strftime('%Y-%m-%d %H:%M:%S %Z')}",
        f"- 対局数: {len(results)}",
        f"- OLD: `{arguments.old_source}`",
        f"- NEW: `{arguments.new_source}`",
        f"- NEW変更内容: {arguments.new_change}",
        "",
        "## Final Summary",
        "",
        "| Bot | Wins | Total Stones | 最大通常depth | 完全読み成功局数 |",
        "|---|---:|---:|---:|---:|",
    ]

    for label in ("OLD", "NEW"):
        available_summaries = [
            result.analytics[label]
            for result in results
            if result.analytics[label].metric_available
        ]
        if available_summaries:
            maximum_depth = max(summary.max_depth for summary in available_summaries)
            perfect_games = sum(1 for summary in available_summaries if summary.perfect_completed)
            depth_text = str(maximum_depth)
            perfect_text = str(perfect_games)
        else:
            depth_text = "N/A"
            perfect_text = "N/A"
        lines.append(
            f"| {label} | {wins[label]} | {total_stones[label]} | "
            f"{depth_text} | {perfect_text} |"
        )

    lines.extend([f"", f"- Draws: {draws}"])
    if wins["OLD"] != wins["NEW"]:
        if wins["OLD"] > wins["NEW"]:
            better = "OLD"
        else:
            better = "NEW"
        lines.append(f"- 総合結果: **{better}**（勝数優位）")
    elif total_stones["OLD"] != total_stones["NEW"]:
        if total_stones["OLD"] > total_stones["NEW"]:
            better = "OLD"
        else:
            better = "NEW"
        lines.append(f"- 総合結果: **{better}**（勝数同数・総石数優位）")
    else:
        lines.append("- 総合結果: **TIE**")

    lines.extend([
        "",
        "## Game Details",
        "",
        "- 定石最終手数: 盤面全体で何手目まで定石手を使用したか",
        "- 最大通常depth: 完全読みではないMinimaxで完了した最大depth",
        "- 完全読み: 終局までの完全探索を1回以上完了できたか（T/F）",
        "- N/A: 対象AIが計測ログを出力していないため取得不可",
        "",
    ])

    for game_number, result in enumerate(results, start=1):
        if result.winner_label is None:
            result_text = "Draw"
        else:
            result_text = f"{result.winner_label} win"
        lines.extend([
            f"### Game {game_number}",
            "",
            f"- 結果: **{result_text}**",
            f"- 理由: {result.reason}",
            "",
            "| Bot | Color | Stones | 定石最終手数 | 最大通常depth | 完全読み |",
            "|---|---|---:|---:|---:|:---:|",
        ])
        for label in ("OLD", "NEW"):
            summary = result.analytics[label]
            lines.append(
                f"| {label} | {color_name(result.colors[label])} | {result.stones[label]} | "
                f"{metric_text(summary.book_last_move, summary.metric_available)} | "
                f"{summary.max_depth if summary.metric_available else 'N/A'} | "
                f"{bool_text(summary.perfect_completed, summary.metric_available)} |"
            )
        lines.append("")
    return "\n".join(lines) + "\n"


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Run local Othello bot matches.")
    parser.add_argument("--old", required=True, type=Path)
    parser.add_argument("--new", required=True, type=Path)
    parser.add_argument("--old-source", default="OLD")
    parser.add_argument("--new-source", default="NEW")
    parser.add_argument("--new-change", required=True)
    parser.add_argument("--games", type=int, default=20)
    parser.add_argument("--result-dir", required=True, type=Path)
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
    results: list[GameResult] = []

    print("Match Information")
    print(f"NEW Change: {arguments.new_change}")
    print(f"Games: {arguments.games}")
    print()

    for game_index in range(arguments.games):
        if game_index % 2 == 0:
            old_color = BLACK
        else:
            old_color = WHITE
        result = play_game(old_executable, new_executable, old_color)
        results.append(result)
        print_game_result(game_index + 1, result)

        if result.winner_label is None:
            draws += 1
        else:
            wins[result.winner_label] += 1
        total_stones["OLD"] += result.stones["OLD"]
        total_stones["NEW"] += result.stones["NEW"]

    print("Final Summary")
    print(f"OLD: Wins={wins['OLD']}, Total Stones={total_stones['OLD']}")
    print(f"NEW: Wins={wins['NEW']}, Total Stones={total_stones['NEW']}")
    print(f"Draws: {draws}")

    arguments.result_dir.mkdir(parents=True, exist_ok=True)
    timestamp = datetime.now().astimezone().strftime("%Y%m%d_%H%M%S")
    change_details = sanitize_filename_component(arguments.new_change)
    report_path = arguments.result_dir / f"{change_details}_{timestamp}_result.md"
    report = create_markdown_report(results, arguments, wins, total_stones, draws)
    report_path.write_text(report, encoding="utf-8")
    print(f"Markdown report: {report_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
