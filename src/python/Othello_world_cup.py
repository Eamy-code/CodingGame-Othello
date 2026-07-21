import sys
import time

PLAYER_ZERO = "0"
PLAYER_ONE = "1"
BOARD_SIZE = 8
ALL_MASK = (1 << 64) - 1
INF = 10 ** 15
PERFECT_EMPTY_LIMIT = 10

FILE_A = 0
FILE_H = 0

for row in range(BOARD_SIZE):
    FILE_A |= 1 << (row * BOARD_SIZE)
    FILE_H |= 1 << (row * BOARD_SIZE + 7)

NOT_A_FILE = ALL_MASK ^ FILE_A
NOT_H_FILE = ALL_MASK ^ FILE_H

POSITION_WEIGHTS = [
    [3000, -500, 10, 5, 5, 10, -500, 3000],
    [-500, -800, -10, -10, -10, -10, -800, -500],
    [10, -10, 20, 0, 0, 20, -10, 10],
    [5, -10, 0, 0, 0, 0, -10, 5],
    [5, -10, 0, 0, 0, 0, -10, 5],
    [10, -10, 20, 0, 0, 20, -10, 10],
    [-500, -800, -10, -10, -10, -10, -800, -500],
    [3000, -500, 10, 5, 5, 10, -500, 3000],
]

MOVE_WEIGHTS = [
    [1000, -800, 100, 50, 50, 100, -800, 1000],
    [-800, -900, -80, -80, -80, -80, -900, -800],
    [100, -80, 20, 5, 5, 20, -80, 100],
    [50, -80, 5, 0, 0, 5, -80, 50],
    [50, -80, 5, 0, 0, 5, -80, 50],
    [100, -80, 20, 5, 5, 20, -80, 100],
    [-800, -900, -80, -80, -80, -80, -900, -800],
    [1000, -800, 100, 50, 50, 100, -800, 1000],
]

POSITION_FLAT = []
MOVE_FLAT = []

for row in range(BOARD_SIZE):
    for col in range(BOARD_SIZE):
        POSITION_FLAT.append(POSITION_WEIGHTS[row][col])
        MOVE_FLAT.append(MOVE_WEIGHTS[row][col])

STANDARD_MOVE_LINES = [
    "f5d6c3g5c6c5c4b6f6f4e6d7c7g6d8b5e7b3a6e3a5d3",
    "f5d6c3g5f6d3e3c2c1e6f4f3f2g4g6d2h3h4h5f7e7g3",
    "f5d6c3g5g6d3c4e3f3b4f6e6f4g4h4h5h6g3h3f7f8c2",
    "f5d6c4b3b4f4f6g5f3e7c5e6c3g4c6g3h3e3f2b6h4d3",
    "f5d6c5b4d7e7c7d8c3d3c4b3d2e2c2e3f4f2c6b5f3c8",
    "f5d6c4b3b4f4f6g5f3e7c5e6c3g4c6g3h3e3f2b6h4d3e2",
    "f5d6c4d3c3b3d2e1b5c5b4e3c2a4c6d1e2c7b6f1e6f3f2",
    "f5d6c4d3c3f4f6f3e6e7f7c5b6g5e3d7c6e2g4h3d2g3f1",
    "f5d6c4d3c3f4f6f3e6e7f7c5b6g6e3e2f1d1g5c6d8g4h6",
    "f5d6c4d3c3f4f6b4c2f3e3e2c6f2c5e6d2g4d7b3g5c8h4",
    "f5d6c4d3c3f4f6g5e3f3g6e2h5c5g4g3f2e1f1g2h4d1d2h3e6",
    "f5d6c4d3c3b5b4f4c5a4b3d2a6a3e3f3g4e6f6g3e2c2f2",
    "f5d6c4g5f6f4f3d3c3g6e3e6h5d2e2c2c6c5b6b4b3c7a4",
    "f5f6e6f4g6c5f3g4e3d6g5g3c3h5c4d7h6h7h3f7e7f8h4",
    "f5f6e6f4g6c5f3g5d6e3h4g3g4h6e2d3h5h3c6e7f2c4d2",
    "f5f6e6f4g6d6g4g5h4e7f3h6f7e8f8g8d3h5h7e3c5c4g3",
    "f5f6e6d6f7e3c6e7f4c5d8c7d7f8b5c4e8c8f3g5b6d3b4",
    "f5f6e6d6f7f4d7e7d8g5c6f8g6h5h6h7c4e8g8c5e3d3c7",
    "f5f4e3f2e2f6d3c4f3e1f1g1e6c5c6d6c3c2g4b4d2d1",
    "f5f4f3d6c4g5f6d3c3g6e3g3e6f2h3h4g4h2h6f7e2",
    "f5f4f3f6d6g4e3e6d3f2g3h4f1e2h3h2e1c5c6c4b6b5c3b4a4d7",
    "f5f4e3f2g4f6d6g5g3f3e6c5c6d3e2e7d7e1c4h4",
]


def debug(message):
    print(message, file=sys.stderr, flush=True)


def opponent_of(player):
    if player == PLAYER_ZERO:
        return PLAYER_ONE
    return PLAYER_ZERO


def bit_index(row, col):
    return row * BOARD_SIZE + col


def bit_mask(row, col):
    return 1 << bit_index(row, col)


def bit_to_position(move_bit):
    position = move_bit.bit_length() - 1
    row = position // BOARD_SIZE
    col = position % BOARD_SIZE
    return row, col


def action_to_position(action):
    action = action.lower()
    col = ord(action[0]) - ord("a")
    row = int(action[1:]) - 1
    return row, col


def action_to_bit(action):
    row, col = action_to_position(action)
    return bit_mask(row, col)


def position_to_action(row, col):
    return chr(ord("a") + col) + str(row + 1)


def bit_to_action(move_bit):
    row, col = bit_to_position(move_bit)
    return position_to_action(row, col)


def shift_north(bits):
    return bits >> 8


def shift_south(bits):
    return (bits << 8) & ALL_MASK


def shift_east(bits):
    return ((bits & NOT_H_FILE) << 1) & ALL_MASK


def shift_west(bits):
    return (bits & NOT_A_FILE) >> 1


def shift_north_east(bits):
    return (bits & NOT_H_FILE) >> 7


def shift_north_west(bits):
    return (bits & NOT_A_FILE) >> 9


def shift_south_east(bits):
    return ((bits & NOT_H_FILE) << 9) & ALL_MASK


def shift_south_west(bits):
    return ((bits & NOT_A_FILE) << 7) & ALL_MASK


SHIFT_FUNCTIONS = [
    shift_north,
    shift_south,
    shift_east,
    shift_west,
    shift_north_east,
    shift_north_west,
    shift_south_east,
    shift_south_west,
]


def neighbor_bits(bits):
    result = 0

    for shift_function in SHIFT_FUNCTIONS:
        result |= shift_function(bits)

    return result & ALL_MASK


def split_standard_line(line):
    moves = []
    index = 0

    while index < len(line):
        moves.append(line[index:index + 2])
        index += 2

    return moves


def transform_position(row, col, transform_id):
    if transform_id == 0:
        return row, col

    if transform_id == 1:
        return col, BOARD_SIZE - 1 - row

    if transform_id == 2:
        return BOARD_SIZE - 1 - row, BOARD_SIZE - 1 - col

    if transform_id == 3:
        return BOARD_SIZE - 1 - col, row

    flipped_row = BOARD_SIZE - 1 - row
    flipped_col = col

    if transform_id == 4:
        return flipped_row, flipped_col

    if transform_id == 5:
        return flipped_col, BOARD_SIZE - 1 - flipped_row

    if transform_id == 6:
        return BOARD_SIZE - 1 - flipped_row, BOARD_SIZE - 1 - flipped_col

    return BOARD_SIZE - 1 - flipped_col, flipped_row


def build_standard_book():
    standard_book = {}

    for line in STANDARD_MOVE_LINES:
        original_moves = split_standard_line(line)

        for transform_id in range(8):
            transformed_moves = []

            for move in original_moves:
                row, col = action_to_position(move)
                transformed_row, transformed_col = transform_position(row, col, transform_id)
                transformed_moves.append(position_to_action(transformed_row, transformed_col))

            for index in range(len(transformed_moves)):
                key = tuple(transformed_moves[:index])

                if key not in standard_book:
                    standard_book[key] = []

                next_move = transformed_moves[index]

                if next_move not in standard_book[key]:
                    standard_book[key].append(next_move)

    return standard_book


STANDARD_BOOK = build_standard_book()


def find_standard_move(history_actions, legal_actions):
    key = tuple(history_actions)

    if key not in STANDARD_BOOK:
        return None

    legal_set = set(legal_actions)

    for move in STANDARD_BOOK[key]:
        if move in legal_set:
            return move

    return None


class Board:
    def __init__(self, black_board, white_board):
        self.black_board = black_board
        self.white_board = white_board

    @classmethod
    def from_lines(cls, board_lines):
        black_board = 0
        white_board = 0

        for row in range(BOARD_SIZE):
            for col in range(BOARD_SIZE):
                cell = board_lines[row][col]
                mask = bit_mask(row, col)

                if cell == PLAYER_ZERO:
                    black_board |= mask
                elif cell == PLAYER_ONE:
                    white_board |= mask

        return cls(black_board, white_board)

    def copy(self):
        return Board(self.black_board, self.white_board)

    def occupied_board(self):
        return self.black_board | self.white_board

    def player_board(self, player):
        if player == PLAYER_ZERO:
            return self.black_board
        return self.white_board

    def opponent_board(self, player):
        if player == PLAYER_ZERO:
            return self.white_board
        return self.black_board

    def count_stones(self, player):
        return self.player_board(player).bit_count()

    def count_empty_cells(self):
        return 64 - self.occupied_board().bit_count()

    def get_cell(self, row, col):
        mask = bit_mask(row, col)

        if (self.black_board & mask) != 0:
            return PLAYER_ZERO

        if (self.white_board & mask) != 0:
            return PLAYER_ONE

        return "."

    def legal_moves_bits(self, player):
        # 空きマス全走査ではなく、8方向シフトで合法手を一括生成します。
        player_bits = self.player_board(player)
        opponent_bits = self.opponent_board(player)
        empty_bits = (~(player_bits | opponent_bits)) & ALL_MASK
        moves = 0

        for shift_function in SHIFT_FUNCTIONS:
            candidates = shift_function(player_bits) & opponent_bits

            for _ in range(5):
                candidates |= shift_function(candidates) & opponent_bits

            moves |= shift_function(candidates) & empty_bits

        return moves & ALL_MASK

    def get_legal_move_bits_list(self, player):
        moves_bits = self.legal_moves_bits(player)
        legal_move_bits = []

        while moves_bits != 0:
            move_bit = moves_bits & -moves_bits
            moves_bits -= move_bit
            legal_move_bits.append(move_bit)

        return legal_move_bits

    def get_flips_for_direction(self, move_bit, shift_function, player_bits, opponent_bits):
        captured = 0
        current = shift_function(move_bit) & opponent_bits

        while current != 0:
            captured |= current
            next_bit = shift_function(current)

            if (next_bit & player_bits) != 0:
                return captured

            if (next_bit & opponent_bits) == 0:
                break

            current = next_bit & opponent_bits

        return 0

    def get_flips_mask_by_bit(self, move_bit, player):
        if (self.occupied_board() & move_bit) != 0:
            return 0

        player_bits = self.player_board(player)
        opponent_bits = self.opponent_board(player)
        flips = 0

        for shift_function in SHIFT_FUNCTIONS:
            flips |= self.get_flips_for_direction(move_bit, shift_function, player_bits, opponent_bits)

        return flips

    def apply_move_bit(self, move_bit, player):
        # undo探索用に、置いた石と反転した石を記録してから盤面を直接更新します。
        flips_mask = self.get_flips_mask_by_bit(move_bit, player)

        if player == PLAYER_ZERO:
            self.black_board |= move_bit | flips_mask
            self.white_board &= ~flips_mask
        else:
            self.white_board |= move_bit | flips_mask
            self.black_board &= ~flips_mask

        return (player, move_bit, flips_mask)

    def undo_move(self, move_record):
        # apply_move_bitで更新した盤面を元に戻します。
        player, move_bit, flips_mask = move_record

        if player == PLAYER_ZERO:
            self.black_board &= ~(move_bit | flips_mask)
            self.white_board |= flips_mask
        else:
            self.white_board &= ~(move_bit | flips_mask)
            self.black_board |= flips_mask


def infer_new_moves(previous_board, current_board, placed_player):
    new_moves = []
    previous_occupied = previous_board.occupied_board()
    current_player_bits = current_board.player_board(placed_player)
    new_bits = current_player_bits & ~previous_occupied

    while new_bits != 0:
        move_bit = new_bits & -new_bits
        new_bits -= move_bit
        new_moves.append(bit_to_action(move_bit))

    return new_moves


class Evaluator:
    def evaluate_board_position(self, board, player):
        player_bits = board.player_board(player)
        opponent_bits = board.opponent_board(player)
        score = 0

        current_bits = player_bits

        while current_bits != 0:
            move_bit = current_bits & -current_bits
            current_bits -= move_bit
            position = move_bit.bit_length() - 1
            score += POSITION_FLAT[position]

        current_bits = opponent_bits

        while current_bits != 0:
            move_bit = current_bits & -current_bits
            current_bits -= move_bit
            position = move_bit.bit_length() - 1
            score -= POSITION_FLAT[position]

        return score

    def evaluate_move_score_by_bit(self, move_bit):
        position = move_bit.bit_length() - 1
        return MOVE_FLAT[position]

    def evaluate_pass(self, my_pass_count, opponent_pass_count):
        return (opponent_pass_count - my_pass_count) * 50

    def evaluate_stone_count(self, board, player):
        opponent = opponent_of(player)
        my_count = board.count_stones(player)
        opponent_count = board.count_stones(opponent)
        empty_count = board.count_empty_cells()

        if my_count == 0:
            return -2147483647

        if empty_count >= 40:
            return (opponent_count - my_count) * 2

        if empty_count >= 15:
            return 0

        return (my_count - opponent_count) * 5

    def evaluate_mobility(self, board, player):
        opponent = opponent_of(player)
        my_moves = board.legal_moves_bits(player).bit_count()
        opponent_moves = board.legal_moves_bits(opponent).bit_count()
        return (my_moves - opponent_moves) * 30

    def evaluate_potential_mobility(self, board, player):
        player_bits = board.player_board(player)
        opponent_bits = board.opponent_board(player)
        empty_bits = (~board.occupied_board()) & ALL_MASK

        my_potential = (neighbor_bits(opponent_bits) & empty_bits).bit_count()
        opponent_potential = (neighbor_bits(player_bits) & empty_bits).bit_count()

        return (my_potential - opponent_potential) * 3

    def evaluate_frontier(self, board, player):
        player_bits = board.player_board(player)
        opponent_bits = board.opponent_board(player)
        empty_neighbors = neighbor_bits((~board.occupied_board()) & ALL_MASK)

        my_frontier = (player_bits & empty_neighbors).bit_count()
        opponent_frontier = (opponent_bits & empty_neighbors).bit_count()

        return (opponent_frontier - my_frontier) * 5

    def evaluate_stability(self, board, player):
        opponent = opponent_of(player)
        my_stable = 0
        opponent_stable = 0
        corners = [(0, 0), (0, 7), (7, 0), (7, 7)]

        for corner_row, corner_col in corners:
            corner_cell = board.get_cell(corner_row, corner_col)

            if corner_cell == ".":
                continue

            corner_stable = 1
            ray_directions = []

            if corner_row == 0:
                ray_directions.append((1, 0))
            else:
                ray_directions.append((-1, 0))

            if corner_col == 0:
                ray_directions.append((0, 1))
            else:
                ray_directions.append((0, -1))

            for row_delta, col_delta in ray_directions:
                current_row = corner_row + row_delta
                current_col = corner_col + col_delta

                while 0 <= current_row < BOARD_SIZE and 0 <= current_col < BOARD_SIZE:
                    if board.get_cell(current_row, current_col) != corner_cell:
                        break

                    corner_stable += 1
                    current_row += row_delta
                    current_col += col_delta

            if corner_cell == player:
                my_stable += corner_stable
            elif corner_cell == opponent:
                opponent_stable += corner_stable

        return (my_stable - opponent_stable) * 30

    def evaluate_all(self, board, player, last_move_bit, my_pass_count, opponent_pass_count):
        score = 0
        score += self.evaluate_board_position(board, player)
        score += self.evaluate_stone_count(board, player)
        score += self.evaluate_mobility(board, player)
        score += self.evaluate_potential_mobility(board, player)
        score += self.evaluate_frontier(board, player)
        score += self.evaluate_stability(board, player)
        score += self.evaluate_pass(my_pass_count, opponent_pass_count)

        if last_move_bit is not None:
            score += self.evaluate_move_score_by_bit(last_move_bit)

        return score


class MiniMaxOthelloAi:
    def __init__(self, player_id):
        self.player = str(player_id)
        self.evaluator = Evaluator()
        self.end_time = 0.0
        self.timed_out = False
        self.perfect_cache = {}

    def is_time_up(self):
        return time.perf_counter() >= self.end_time

    def sort_moves(self, move_bits):
        move_bits.sort(key=self.evaluator.evaluate_move_score_by_bit, reverse=True)

    def choose_move(self, board, legal_actions):
        if len(legal_actions) == 0:
            return "PASS"

        legal_move_bits = []

        for action in legal_actions:
            legal_move_bits.append(action_to_bit(action))

        best_move_bit = legal_move_bits[0]
        placed_count = board.count_stones(PLAYER_ZERO) + board.count_stones(PLAYER_ONE)
        empty_count = board.count_empty_cells()

        time_limit_seconds = 0.12

        if placed_count <= 5:
            time_limit_seconds = 1.75

        self.end_time = time.perf_counter() + time_limit_seconds
        self.timed_out = False
        self.perfect_cache = {}
        self.sort_moves(legal_move_bits)

        if empty_count <= PERFECT_EMPTY_LIMIT:
            current_best_score = -INF
            alpha = -INF
            beta = INF

            for move_bit in legal_move_bits:
                if self.is_time_up():
                    self.timed_out = True
                    break

                move_record = board.apply_move_bit(move_bit, self.player)
                score = self.perfect_play(board, opponent_of(self.player), self.player, alpha, beta)
                board.undo_move(move_record)

                if self.timed_out:
                    break

                if score > current_best_score:
                    current_best_score = score
                    best_move_bit = move_bit

                if score > alpha:
                    alpha = score

            if not self.timed_out:
                debug("perfect score=" + str(current_best_score))
                return bit_to_action(best_move_bit)

        depth = 1
        max_depth = 9

        while depth <= max_depth:
            if self.is_time_up():
                break

            current_best_move_bit = None
            current_best_score = -INF
            alpha = -INF
            beta = INF

            for move_bit in legal_move_bits:
                if self.is_time_up():
                    self.timed_out = True
                    break

                move_record = board.apply_move_bit(move_bit, self.player)

                score = self.minimax(
                    board,
                    depth - 1,
                    alpha,
                    beta,
                    opponent_of(self.player),
                    self.player,
                    0,
                    0,
                    move_bit,
                )

                board.undo_move(move_record)

                if self.timed_out:
                    break

                if current_best_move_bit is None or score > current_best_score:
                    current_best_score = score
                    current_best_move_bit = move_bit

                if score > alpha:
                    alpha = score

            if not self.timed_out and current_best_move_bit is not None:
                best_move_bit = current_best_move_bit
                debug("depth=" + str(depth) + " score=" + str(current_best_score))
                depth += 1
                continue

            break

        return bit_to_action(best_move_bit)

    def minimax(self, board, depth, alpha, beta, current_player, root_player, my_pass_count, opponent_pass_count, last_move_bit):
        if self.is_time_up():
            self.timed_out = True
            return self.evaluator.evaluate_all(board, root_player, last_move_bit, my_pass_count, opponent_pass_count)

        empty_count = board.count_empty_cells()

        if empty_count <= PERFECT_EMPTY_LIMIT:
            return self.perfect_play(board, current_player, root_player, alpha, beta)

        legal_move_bits = board.get_legal_move_bits_list(current_player)
        opponent_player = opponent_of(current_player)

        if depth <= 0:
            return self.evaluator.evaluate_all(board, root_player, last_move_bit, my_pass_count, opponent_pass_count)

        if len(legal_move_bits) == 0:
            opponent_moves_bits = board.legal_moves_bits(opponent_player)

            if opponent_moves_bits == 0:
                return self.final_score(board, root_player)

            if current_player == root_player:
                return self.minimax(
                    board,
                    depth - 1,
                    alpha,
                    beta,
                    opponent_player,
                    root_player,
                    my_pass_count + 1,
                    opponent_pass_count,
                    None,
                )

            return self.minimax(
                board,
                depth - 1,
                alpha,
                beta,
                opponent_player,
                root_player,
                my_pass_count,
                opponent_pass_count + 1,
                None,
            )

        self.sort_moves(legal_move_bits)

        if current_player == root_player:
            best_score = -INF

            for move_bit in legal_move_bits:
                if self.is_time_up():
                    self.timed_out = True
                    break

                move_record = board.apply_move_bit(move_bit, current_player)

                score = self.minimax(
                    board,
                    depth - 1,
                    alpha,
                    beta,
                    opponent_player,
                    root_player,
                    my_pass_count,
                    opponent_pass_count,
                    move_bit,
                )

                board.undo_move(move_record)

                if score > best_score:
                    best_score = score

                if score > alpha:
                    alpha = score

                if beta <= alpha:
                    break

            return best_score

        best_score = INF

        for move_bit in legal_move_bits:
            if self.is_time_up():
                self.timed_out = True
                break

            move_record = board.apply_move_bit(move_bit, current_player)

            score = self.minimax(
                board,
                depth - 1,
                alpha,
                beta,
                opponent_player,
                root_player,
                my_pass_count,
                opponent_pass_count,
                move_bit,
            )

            board.undo_move(move_record)

            if score < best_score:
                best_score = score

            if score < beta:
                beta = score

            if beta <= alpha:
                break

        return best_score

    def perfect_play(self, board, current_player, root_player, alpha, beta):
        if self.is_time_up():
            self.timed_out = True
            return self.final_score(board, root_player)

        cache_key = (board.black_board, board.white_board, current_player, root_player)

        if cache_key in self.perfect_cache:
            return self.perfect_cache[cache_key]

        legal_move_bits = board.get_legal_move_bits_list(current_player)
        opponent_player = opponent_of(current_player)

        if len(legal_move_bits) == 0:
            opponent_moves_bits = board.legal_moves_bits(opponent_player)

            if opponent_moves_bits == 0:
                score = self.final_score(board, root_player)
                self.perfect_cache[cache_key] = score
                return score

            score = self.perfect_play(board, opponent_player, root_player, alpha, beta)

            if not self.timed_out:
                self.perfect_cache[cache_key] = score

            return score

        self.sort_moves(legal_move_bits)

        if current_player == root_player:
            best_score = -INF

            for move_bit in legal_move_bits:
                if self.is_time_up():
                    self.timed_out = True
                    break

                move_record = board.apply_move_bit(move_bit, current_player)
                score = self.perfect_play(board, opponent_player, root_player, alpha, beta)
                board.undo_move(move_record)

                if score > best_score:
                    best_score = score

                if score > alpha:
                    alpha = score

                if beta <= alpha:
                    break

            if not self.timed_out:
                self.perfect_cache[cache_key] = best_score

            return best_score

        best_score = INF

        for move_bit in legal_move_bits:
            if self.is_time_up():
                self.timed_out = True
                break

            move_record = board.apply_move_bit(move_bit, current_player)
            score = self.perfect_play(board, opponent_player, root_player, alpha, beta)
            board.undo_move(move_record)

            if score < best_score:
                best_score = score

            if score < beta:
                beta = score

            if beta <= alpha:
                break

        if not self.timed_out:
            self.perfect_cache[cache_key] = best_score

        return best_score

    def final_score(self, board, player):
        opponent = opponent_of(player)
        my_count = board.count_stones(player)
        opponent_count = board.count_stones(opponent)
        return (my_count - opponent_count) * 100000


def main():
    player_id = int(input())
    board_size = int(input())

    my_player = str(player_id)
    opponent_player = opponent_of(my_player)
    ai = MiniMaxOthelloAi(player_id)

    history_actions = []
    last_simulated_board = None
    is_first_turn = True

    initial_board = Board.from_lines([
        "........",
        "........",
        "........",
        "...01...",
        "...10...",
        "........",
        "........",
        "........",
    ])

    while True:
        board_lines = []

        for _ in range(board_size):
            board_lines.append(input())

        action_count = int(input())
        actions = []

        for _ in range(action_count):
            actions.append(input().lower())

        current_board = Board.from_lines(board_lines)

        if last_simulated_board is not None:
            opponent_moves = infer_new_moves(last_simulated_board, current_board, opponent_player)

            for opponent_move in opponent_moves:
                history_actions.append(opponent_move)

        if is_first_turn:
            if my_player == PLAYER_ONE:
                first_opponent_moves = infer_new_moves(initial_board, current_board, opponent_player)

                for opponent_move in first_opponent_moves:
                    if opponent_move not in history_actions:
                        history_actions.append(opponent_move)

            is_first_turn = False

        selected_action = None
        standard_move = find_standard_move(history_actions, actions)

        if standard_move is not None:
            selected_action = standard_move
            debug("standard=" + selected_action)

        if selected_action is None:
            selected_action = ai.choose_move(current_board, actions)

        if selected_action != "PASS":
            history_actions.append(selected_action)
            selected_bit = action_to_bit(selected_action)
            simulated_board = current_board.copy()
            simulated_board.apply_move_bit(selected_bit, my_player)
            last_simulated_board = simulated_board
        else:
            last_simulated_board = current_board.copy()

        print(selected_action, flush=True)


if __name__ == "__main__":
    main()