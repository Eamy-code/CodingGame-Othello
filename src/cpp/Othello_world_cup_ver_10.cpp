#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

using Bitboard = std::uint64_t;
using Score = long long;
using Clock = std::chrono::steady_clock;

constexpr char PLAYER_ZERO = '0';
constexpr char PLAYER_ONE = '1';
constexpr int BOARD_SIZE = 8;
constexpr Bitboard ALL_MASK = std::numeric_limits<Bitboard>::max();
constexpr Score INF = 1000000000000000LL;
constexpr int PERFECT_EMPTY_LIMIT = 10;
constexpr Score ODD_EMPTY_REGION_MOVE_ORDER_BONUS = 600;
constexpr std::uint32_t TIME_CHECK_NODE_INTERVAL = 64;
constexpr std::size_t MAX_TRANSPOSITION_ENTRIES = 250000;
constexpr std::size_t MAX_PERFECT_CACHE_ENTRIES = 250000;

constexpr Bitboard FILE_A = 0x0101010101010101ULL;
constexpr Bitboard FILE_H = 0x8080808080808080ULL;
constexpr Bitboard NOT_A_FILE = ALL_MASK ^ FILE_A;
constexpr Bitboard NOT_H_FILE = ALL_MASK ^ FILE_H;

// Derived from Nyanyan/OthelloAI_Textbook cell_evaluation.hpp.
// Copyright (c) 2021 Takuto Yamana, MIT License.
// Values are scaled and rounded for this evaluation function.
constexpr std::array<int, 64> POSITION_WEIGHTS = {
    100,   5,   3,  -1,  -1,   3,   5, 100,
      5, -21,  -7,  -6,  -6,  -7, -21,   5,
      3,  -7, -14,  -5,  -5, -14,  -7,   3,
     -1,  -6,  -5,  -6,  -6,  -5,  -6,  -1,
     -1,  -6,  -5,  -6,  -6,  -5,  -6,  -1,
      3,  -7, -14,  -5,  -5, -14,  -7,   3,
      5, -21,  -7,  -6,  -6,  -7, -21,   5,
    100,   5,   3,  -1,  -1,   3,   5, 100
};

const std::array<int, 64> MOVE_WEIGHTS = {
    1000, -800, 100, 50, 50, 100, -800, 1000,
    -800, -900, -80, -80, -80, -80, -900, -800,
    100, -80, 20, 5, 5, 20, -80, 100,
    50, -80, 5, 0, 0, 5, -80, 50,
    50, -80, 5, 0, 0, 5, -80, 50,
    100, -80, 20, 5, 5, 20, -80, 100,
    -800, -900, -80, -80, -80, -80, -900, -800,
    1000, -800, 100, 50, 50, 100, -800, 1000
};

constexpr Score POSITION_EVALUATION_WEIGHT = 5;
constexpr Score STONE_DIFFERENCE_WEIGHT = 1;
constexpr Score MOBILITY_WEIGHT = 10;
constexpr Score POTENTIAL_MOBILITY_WEIGHT = 5;
constexpr Score FRONTIER_WEIGHT = 7;
constexpr Score STABILITY_WEIGHT = 12;
constexpr Score PASS_WEIGHT = 100;

constexpr Score EDGE_WEDGE_MOVE_BONUS = 200;
constexpr Score EDGE_WEDGE_POSITION_BONUS = 120;
constexpr Score MULTI_EMPTY_EDGE_GAP_MOVE_PENALTY = -160;
constexpr Score OPPONENT_MOVE_ORDER_PENALTY = 60;
constexpr Score OPPONENT_POTENTIAL_MOVE_ORDER_PENALTY = 8;
constexpr Score OPPONENT_PASS_MOVE_ORDER_BONUS = 900;
constexpr Score OPPONENT_CORNER_MOVE_ORDER_PENALTY = 2500;
constexpr Score CORNER_DIAGONAL_PATTERN_WEIGHT = 4;

constexpr int EMPTY_CORNER_C_WEIGHT = -20;
constexpr int EMPTY_CORNER_X_WEIGHT = -40;
constexpr int OCCUPIED_CORNER_C_WEIGHT = 0;
constexpr int OCCUPIED_CORNER_X_WEIGHT = 0;

constexpr std::array<std::array<int, 8>, 4> EDGE_POSITIONS = {{
    {{0, 1, 2, 3, 4, 5, 6, 7}},
    {{56, 57, 58, 59, 60, 61, 62, 63}},
    {{0, 8, 16, 24, 32, 40, 48, 56}},
    {{7, 15, 23, 31, 39, 47, 55, 63}}
}};

// 複数空き危険判定で、各角を起点に辺をたどる有方向配列です。
constexpr std::array<std::array<int, 8>, 8> CORNER_EDGE_RAYS = {{
    {{0, 1, 2, 3, 4, 5, 6, 7}},          // a1 -> h1
    {{0, 8, 16, 24, 32, 40, 48, 56}},    // a1 -> a8
    {{7, 6, 5, 4, 3, 2, 1, 0}},          // h1 -> a1
    {{7, 15, 23, 31, 39, 47, 55, 63}},   // h1 -> h8
    {{56, 57, 58, 59, 60, 61, 62, 63}},  // a8 -> h8
    {{56, 48, 40, 32, 24, 16, 8, 0}},    // a8 -> a1
    {{63, 62, 61, 60, 59, 58, 57, 56}},  // h8 -> a8
    {{63, 55, 47, 39, 31, 23, 15, 7}}    // h8 -> h1
}};

struct CornerRayReference {
    int rayIndex;
    int positionInRay;
};

struct CornerRayReferences {
    std::array<CornerRayReference, 2> references;
    int count;
};

constexpr std::array<CornerRayReferences, 64> buildCornerRayReferences() {
    std::array<CornerRayReferences, 64> result = {};

    for (int rayIndex = 0; rayIndex < 8; ++rayIndex) {
        for (int positionInRay = 1; positionInRay < BOARD_SIZE - 1; ++positionInRay) {
            int position = CORNER_EDGE_RAYS[rayIndex][positionInRay];
            CornerRayReferences& positionReferences = result[position];
            positionReferences.references[positionReferences.count] = {
                rayIndex,
                positionInRay
            };
            ++positionReferences.count;
        }
    }
    return result;
}

constexpr std::array<CornerRayReferences, 64> CORNER_RAY_REFERENCES =
    buildCornerRayReferences();

struct DangerousRayCandidate {
    int rayIndex;
    int moveIndex;
    int gapStart;
    int gapEnd;
    Bitboard gapBits;
};

const std::vector<std::string> STANDARD_MOVE_LINES = {
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
    "f5f4e3f2g4f6d6g5g3f3e6c5c6d3e2e7d7e1c4h4"
};

using ShiftFunction = Bitboard (*)(Bitboard);

// デバッグ情報を標準エラー出力へ表示します。
void debug(const std::string& message) {
    std::cerr << message << std::endl;
}

// 指定プレイヤーの相手プレイヤーを返します。
char opponentOf(char player) {
    if (player == PLAYER_ZERO) {
        return PLAYER_ONE;
    }
    return PLAYER_ZERO;
}

// 行・列からビットボード上のインデックスを計算します。
int bitIndex(int row, int column) {
    return row * BOARD_SIZE + column;
}

// 行・列に対応する1ビットのマスクを生成します。
Bitboard bitMask(int row, int column) {
    return 1ULL << bitIndex(row, column);
}

// 1ビットの手から行・列を取得します。
std::pair<int, int> bitToPosition(Bitboard moveBit) {
    int position = __builtin_ctzll(moveBit);
    return {position / BOARD_SIZE, position % BOARD_SIZE};
}

// "f5"形式の手を行・列へ変換します。
std::pair<int, int> actionToPosition(const std::string& action) {
    int column = action[0] - 'a';
    int row = std::stoi(action.substr(1)) - 1;
    return {row, column};
}

// "f5"形式の手を1ビットのマスクへ変換します。
Bitboard actionToBit(const std::string& action) {
    std::pair<int, int> position = actionToPosition(action);
    return bitMask(position.first, position.second);
}

// 行・列を"f5"形式の手へ変換します。
std::string positionToAction(int row, int column) {
    std::string action;
    action.push_back(static_cast<char>('a' + column));
    action += std::to_string(row + 1);
    return action;
}

// 1ビットの手を"f5"形式へ変換します。
std::string bitToAction(Bitboard moveBit) {
    std::pair<int, int> position = bitToPosition(moveBit);
    return positionToAction(position.first, position.second);
}

// ビット列を北方向へ1マス移動します。
Bitboard shiftNorth(Bitboard bits) {
    return bits >> 8;
}

// ビット列を南方向へ1マス移動します。
Bitboard shiftSouth(Bitboard bits) {
    return bits << 8;
}

// ビット列を東方向へ1マス移動します。
Bitboard shiftEast(Bitboard bits) {
    return (bits & NOT_H_FILE) << 1;
}

// ビット列を西方向へ1マス移動します。
Bitboard shiftWest(Bitboard bits) {
    return (bits & NOT_A_FILE) >> 1;
}

// ビット列を北東方向へ1マス移動します。
Bitboard shiftNorthEast(Bitboard bits) {
    return (bits & NOT_H_FILE) >> 7;
}

// ビット列を北西方向へ1マス移動します。
Bitboard shiftNorthWest(Bitboard bits) {
    return (bits & NOT_A_FILE) >> 9;
}

// ビット列を南東方向へ1マス移動します。
Bitboard shiftSouthEast(Bitboard bits) {
    return (bits & NOT_H_FILE) << 9;
}

// ビット列を南西方向へ1マス移動します。
Bitboard shiftSouthWest(Bitboard bits) {
    return (bits & NOT_A_FILE) << 7;
}

const std::array<ShiftFunction, 8> SHIFT_FUNCTIONS = {
    shiftNorth,
    shiftSouth,
    shiftEast,
    shiftWest,
    shiftNorthEast,
    shiftNorthWest,
    shiftSouthEast,
    shiftSouthWest
};

// 指定ビット群に8方向で隣接する全マスを求めます。
Bitboard neighborBits(Bitboard bits) {
    Bitboard result = 0;
    for (ShiftFunction shiftFunction : SHIFT_FUNCTIONS) {
        result |= shiftFunction(bits);
    }
    return result;
}

// 2つの非負値の差を共通スケールへ正規化します。
Score normalizedDifference(int myValue, int opponentValue) {
    return static_cast<Score>(100) * (myValue - opponentValue)
        / (myValue + opponentValue + 1);
}

// 連結された定石文字列を2文字単位の手へ分割します。
std::vector<std::string> splitStandardLine(const std::string& line) {
    std::vector<std::string> moves;
    for (std::size_t index = 0; index < line.size(); index += 2) {
        moves.push_back(line.substr(index, 2));
    }
    return moves;
}

// 盤面座標に回転・反転の対称変換を適用します。
std::pair<int, int> transformPosition(int row, int column, int transformId) {
    if (transformId == 0) {
        return {row, column};
    }
    if (transformId == 1) {
        return {column, BOARD_SIZE - 1 - row};
    }
    if (transformId == 2) {
        return {BOARD_SIZE - 1 - row, BOARD_SIZE - 1 - column};
    }
    if (transformId == 3) {
        return {BOARD_SIZE - 1 - column, row};
    }

    int flippedRow = BOARD_SIZE - 1 - row;
    int flippedColumn = column;

    if (transformId == 4) {
        return {flippedRow, flippedColumn};
    }
    if (transformId == 5) {
        return {flippedColumn, BOARD_SIZE - 1 - flippedRow};
    }
    if (transformId == 6) {
        return {BOARD_SIZE - 1 - flippedRow, BOARD_SIZE - 1 - flippedColumn};
    }
    return {BOARD_SIZE - 1 - flippedColumn, flippedRow};
}

struct MoveRecord {
    char player;
    Bitboard moveBit;
    Bitboard flipsMask;
};

class Board {
public:
    Bitboard blackBoard;
    Bitboard whiteBoard;

    // 黒石・白石のビットボードから盤面を生成します。
    Board(Bitboard blackBoardValue, Bitboard whiteBoardValue)
        : blackBoard(blackBoardValue), whiteBoard(whiteBoardValue) {
    }

    // 8行の盤面文字列からビットボード盤面を生成します。
    static Board fromLines(const std::vector<std::string>& boardLines) {
        Bitboard blackBoardValue = 0;
        Bitboard whiteBoardValue = 0;

        for (int row = 0; row < BOARD_SIZE; ++row) {
            for (int column = 0; column < BOARD_SIZE; ++column) {
                char cell = boardLines[row][column];
                Bitboard mask = bitMask(row, column);

                if (cell == PLAYER_ZERO) {
                    blackBoardValue |= mask;
                } else if (cell == PLAYER_ONE) {
                    whiteBoardValue |= mask;
                }
            }
        }
        return Board(blackBoardValue, whiteBoardValue);
    }

    // 現在の盤面を複製します。
    Board copy() const {
        return Board(blackBoard, whiteBoard);
    }

    // 石が置かれている全マスを返します。
    Bitboard occupiedBoard() const {
        return blackBoard | whiteBoard;
    }

    // 指定プレイヤーの石だけを返します。
    Bitboard playerBoard(char player) const {
        if (player == PLAYER_ZERO) {
            return blackBoard;
        }
        return whiteBoard;
    }

    // 指定プレイヤーの相手側の石だけを返します。
    Bitboard opponentBoard(char player) const {
        if (player == PLAYER_ZERO) {
            return whiteBoard;
        }
        return blackBoard;
    }

    // 指定プレイヤーの石数を数えます。
    int countStones(char player) const {
        return __builtin_popcountll(playerBoard(player));
    }

    // 空きマス数を数えます。
    int countEmptyCells() const {
        return 64 - __builtin_popcountll(occupiedBoard());
    }

    // 8方向のビットシフトを使い、指定プレイヤーの合法手を一括生成します。
    Bitboard legalMovesBits(char player) const {
        Bitboard playerBits = playerBoard(player);
        Bitboard opponentBits = opponentBoard(player);
        Bitboard emptyBits = ~(playerBits | opponentBits);
        return legalMovesForDirection<shiftNorth>(playerBits, opponentBits, emptyBits)
            | legalMovesForDirection<shiftSouth>(playerBits, opponentBits, emptyBits)
            | legalMovesForDirection<shiftEast>(playerBits, opponentBits, emptyBits)
            | legalMovesForDirection<shiftWest>(playerBits, opponentBits, emptyBits)
            | legalMovesForDirection<shiftNorthEast>(playerBits, opponentBits, emptyBits)
            | legalMovesForDirection<shiftNorthWest>(playerBits, opponentBits, emptyBits)
            | legalMovesForDirection<shiftSouthEast>(playerBits, opponentBits, emptyBits)
            | legalMovesForDirection<shiftSouthWest>(playerBits, opponentBits, emptyBits);
    }

    // 合法手ビットボードを1手ずつのビット配列へ分解します。
    std::vector<Bitboard> getLegalMoveBitsList(char player) const {
        Bitboard movesBits = legalMovesBits(player);
        std::vector<Bitboard> legalMoveBits;
        legalMoveBits.reserve(__builtin_popcountll(movesBits));

        while (movesBits != 0) {
            Bitboard moveBit = movesBits & (~movesBits + 1ULL);
            movesBits -= moveBit;
            legalMoveBits.push_back(moveBit);
        }
        return legalMoveBits;
    }

    // 1方向について、指定手で反転できる相手石を求めます。
    template<ShiftFunction shiftFunction>
    static Bitboard getFlipsForDirection(
        Bitboard moveBit,
        Bitboard playerBits,
        Bitboard opponentBits
    ) {
        Bitboard captured = 0;
        Bitboard current = shiftFunction(moveBit) & opponentBits;

        while (current != 0) {
            captured |= current;
            Bitboard nextBit = shiftFunction(current);

            if ((nextBit & playerBits) != 0) {
                return captured;
            }
            if ((nextBit & opponentBits) == 0) {
                break;
            }
            current = nextBit & opponentBits;
        }
        return 0;
    }

    // 指定手で8方向に反転する石をまとめて返します。
    Bitboard getFlipsMaskByBit(Bitboard moveBit, char player) const {
        if ((occupiedBoard() & moveBit) != 0) {
            return 0;
        }

        Bitboard playerBits = playerBoard(player);
        Bitboard opponentBits = opponentBoard(player);
        return getFlipsForDirection<shiftNorth>(moveBit, playerBits, opponentBits)
            | getFlipsForDirection<shiftSouth>(moveBit, playerBits, opponentBits)
            | getFlipsForDirection<shiftEast>(moveBit, playerBits, opponentBits)
            | getFlipsForDirection<shiftWest>(moveBit, playerBits, opponentBits)
            | getFlipsForDirection<shiftNorthEast>(moveBit, playerBits, opponentBits)
            | getFlipsForDirection<shiftNorthWest>(moveBit, playerBits, opponentBits)
            | getFlipsForDirection<shiftSouthEast>(moveBit, playerBits, opponentBits)
            | getFlipsForDirection<shiftSouthWest>(moveBit, playerBits, opponentBits);
    }

    // 手を盤面へ適用し、undo用の更新内容を返します。
    MoveRecord applyMoveBit(Bitboard moveBit, char player) {
        Bitboard flipsMask = getFlipsMaskByBit(moveBit, player);

        if (player == PLAYER_ZERO) {
            blackBoard |= moveBit | flipsMask;
            whiteBoard &= ~flipsMask;
        } else {
            whiteBoard |= moveBit | flipsMask;
            blackBoard &= ~flipsMask;
        }
        return MoveRecord{player, moveBit, flipsMask};
    }

    // applyMoveBitで適用した手を元に戻します。
    void undoMove(const MoveRecord& moveRecord) {
        if (moveRecord.player == PLAYER_ZERO) {
            blackBoard &= ~(moveRecord.moveBit | moveRecord.flipsMask);
            whiteBoard |= moveRecord.flipsMask;
        } else {
            whiteBoard &= ~(moveRecord.moveBit | moveRecord.flipsMask);
            blackBoard |= moveRecord.flipsMask;
        }
    }

private:
    template<ShiftFunction shiftFunction>
    static Bitboard legalMovesForDirection(
        Bitboard playerBits,
        Bitboard opponentBits,
        Bitboard emptyBits
    ) {
        Bitboard candidates = shiftFunction(playerBits) & opponentBits;
        for (int repeat = 0; repeat < 5; ++repeat) {
            candidates |= shiftFunction(candidates) & opponentBits;
        }
        return shiftFunction(candidates) & emptyBits;
    }
};

struct BookPositionKey {
    Bitboard blackBoard;
    Bitboard whiteBoard;
    char currentPlayer;

    bool operator==(const BookPositionKey& other) const {
        return blackBoard == other.blackBoard
            && whiteBoard == other.whiteBoard
            && currentPlayer == other.currentPlayer;
    }
};

struct BookPositionKeyHash {
    std::size_t operator()(const BookPositionKey& key) const {
        std::size_t hashValue = std::hash<Bitboard>{}(key.blackBoard);
        hashValue ^= std::hash<Bitboard>{}(key.whiteBoard)
            + 0x9e3779b9 + (hashValue << 6) + (hashValue >> 2);
        hashValue ^= std::hash<int>{}(key.currentPlayer)
            + 0x9e3779b9 + (hashValue << 6) + (hashValue >> 2);
        return hashValue;
    }
};

using BookMoveCounts = std::vector<std::pair<Bitboard, int>>;

// 既存の定石棋譜を盤面単位で集計し、転置した手順でも参照できるようにします。
std::unordered_map<BookPositionKey, BookMoveCounts, BookPositionKeyHash>
buildPositionBook() {
    std::unordered_map<BookPositionKey, BookMoveCounts, BookPositionKeyHash> book;
    const Board initialBoard = Board::fromLines({
        "........", "........", "........", "...10...",
        "...01...", "........", "........", "........"
    });

    for (const std::string& line : STANDARD_MOVE_LINES) {
        std::vector<std::string> originalMoves = splitStandardLine(line);
        for (int transformId = 0; transformId < 8; ++transformId) {
            Board board(0, 0);
            for (int position = 0; position < 64; ++position) {
                Bitboard originalBit = 1ULL << position;
                std::pair<int, int> transformed = transformPosition(
                    position / BOARD_SIZE, position % BOARD_SIZE, transformId
                );
                Bitboard transformedBit = bitMask(transformed.first, transformed.second);
                if ((initialBoard.blackBoard & originalBit) != 0) {
                    board.blackBoard |= transformedBit;
                } else if ((initialBoard.whiteBoard & originalBit) != 0) {
                    board.whiteBoard |= transformedBit;
                }
            }
            char currentPlayer = PLAYER_ZERO;
            for (const std::string& move : originalMoves) {
                std::pair<int, int> position = actionToPosition(move);
                std::pair<int, int> transformed = transformPosition(
                    position.first, position.second, transformId
                );
                Bitboard moveBit = bitMask(transformed.first, transformed.second);
                if ((board.legalMovesBits(currentPlayer) & moveBit) == 0) {
                    break;
                }

                BookPositionKey key{board.blackBoard, board.whiteBoard, currentPlayer};
                BookMoveCounts& counts = book[key];
                auto candidate = std::find_if(
                    counts.begin(), counts.end(),
                    [moveBit](const std::pair<Bitboard, int>& item) {
                        return item.first == moveBit;
                    }
                );
                if (candidate == counts.end()) {
                    counts.push_back({moveBit, 1});
                } else {
                    ++candidate->second;
                }
                board.applyMoveBit(moveBit, currentPlayer);
                currentPlayer = opponentOf(currentPlayer);
            }
        }
    }
    return book;
}

const auto POSITION_BOOK = buildPositionBook();

std::string findPositionBookMove(
    const Board& board,
    char currentPlayer,
    const std::vector<std::string>& legalActions
) {
    BookPositionKey key{board.blackBoard, board.whiteBoard, currentPlayer};
    auto bookIterator = POSITION_BOOK.find(key);
    if (bookIterator == POSITION_BOOK.end()) {
        return "";
    }

    Bitboard bestMoveBit = 0;
    int bestCount = 0;
    for (const std::pair<Bitboard, int>& candidate : bookIterator->second) {
        std::string action = bitToAction(candidate.first);
        if (candidate.second > bestCount
            && std::find(legalActions.begin(), legalActions.end(), action) != legalActions.end()) {
            bestMoveBit = candidate.first;
            bestCount = candidate.second;
        }
    }
    return bestMoveBit == 0 ? "" : bitToAction(bestMoveBit);
}

// 公開された学習済み係数を16bitへ量子化し、起動時に形評価表を作ります。
class LearnedPatternEvaluator {
private:
    struct PatternNetwork {
        int patternSize;
        float firstWeights[16][20] = {};
        float firstBias[16] = {};
        float secondWeights[16][16] = {};
        float secondBias[16] = {};
        float outputWeights[16] = {};
        float outputBias = 0;
    };

    std::array<PatternNetwork, 3> networks;
    std::array<std::vector<float>, 3> patternValues;
    std::array<float, 3> patternWeights = {};

    static float activate(float value) {
        return value >= 0 ? value : value * 0.01f;
    }

    static std::vector<float> decodeWeights() {
        const std::string encoded =
            "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"
            "Rf0kAm3Xd/ZLExLS3/4v8Jf+NwNv9Jj+";
        const std::string alphabet =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::vector<float> values;
        values.reserve(1857);
        int buffer = 0;
        int bitCount = 0;
        int firstByte = -1;
        for (char character : encoded) {
            std::size_t digit = alphabet.find(character);
            if (digit == std::string::npos) {
                continue;
            }
            buffer = (buffer << 6) | static_cast<int>(digit);
            bitCount += 6;
            if (bitCount < 8) {
                continue;
            }
            bitCount -= 8;
            int nextByte = (buffer >> bitCount) & 255;
            buffer &= (1 << bitCount) - 1;
            if (firstByte < 0) {
                firstByte = nextByte;
            } else {
                int packedValue = firstByte | (nextByte << 8);
                int signedValue = packedValue >= 32768 ? packedValue - 65536 : packedValue;
                values.push_back(static_cast<float>(signedValue) / 4096.0f);
                firstByte = -1;
            }
        }
        return values;
    }

    static float predict(const PatternNetwork& network, int patternIndex) {
        int digits[10] = {};
        for (int index = network.patternSize - 1; index >= 0; --index) {
            digits[index] = patternIndex % 3;
            patternIndex /= 3;
        }

        float firstLayer[16] = {};
        for (int node = 0; node < 16; ++node) {
            float value = network.firstBias[node];
            for (int index = 0; index < network.patternSize; ++index) {
                if (digits[index] == 0) {
                    value += network.firstWeights[node][index];
                } else if (digits[index] == 1) {
                    value += network.firstWeights[node][network.patternSize + index];
                }
            }
            firstLayer[node] = activate(value);
        }

        float score = network.outputBias;
        for (int node = 0; node < 16; ++node) {
            float value = network.secondBias[node];
            for (int previousNode = 0; previousNode < 16; ++previousNode) {
                value += firstLayer[previousNode]
                    * network.secondWeights[node][previousNode];
            }
            score += activate(value) * network.outputWeights[node];
        }
        return activate(score);
    }

    static int reflectedIndex(int patternType, int patternSize, int patternIndex) {
        int digits[10] = {};
        for (int index = patternSize - 1; index >= 0; --index) {
            digits[index] = patternIndex % 3;
            patternIndex /= 3;
        }
        constexpr std::array<int, 10> triangleReflection = {
            0, 4, 7, 9, 1, 5, 8, 2, 6, 3
        };
        int result = 0;
        for (int index = 0; index < patternSize; ++index) {
            int sourceIndex = patternType == 2
                ? triangleReflection[index] : patternSize - 1 - index;
            result = result * 3 + digits[sourceIndex];
        }
        return result;
    }

    static int boardPatternIndex(
        const Board& board,
        const std::array<int, 10>& positions,
        int patternSize
    ) {
        int result = 0;
        for (int index = 0; index < patternSize; ++index) {
            Bitboard positionBit = 1ULL << positions[index];
            int digit = (board.blackBoard & positionBit) != 0 ? 0
                : ((board.whiteBoard & positionBit) != 0 ? 1 : 2);
            result = result * 3 + digit;
        }
        return result;
    }

public:
    LearnedPatternEvaluator() {
        std::vector<float> values = decodeWeights();
        if (values.size() != 1857) {
            return;
        }
        std::size_t valueIndex = 0;
        for (int patternType = 0; patternType < 3; ++patternType) {
            PatternNetwork& network = networks[patternType];
            network.patternSize = patternType == 0 ? 8 : 10;
            for (int node = 0; node < 16; ++node) {
                for (int index = 0; index < network.patternSize * 2; ++index) {
                    network.firstWeights[node][index] = values[valueIndex++];
                }
            }
            for (float& bias : network.firstBias) {
                bias = values[valueIndex++];
            }
            for (auto& row : network.secondWeights) {
                for (float& weight : row) {
                    weight = values[valueIndex++];
                }
            }
            for (float& bias : network.secondBias) {
                bias = values[valueIndex++];
            }
            for (float& weight : network.outputWeights) {
                weight = values[valueIndex++];
            }
            network.outputBias = values[valueIndex++];

            int patternCount = network.patternSize == 8 ? 6561 : 59049;
            std::vector<float> rawValues(patternCount);
            patternValues[patternType].resize(patternCount);
            for (int index = 0; index < patternCount; ++index) {
                rawValues[index] = predict(network, index);
            }
            for (int index = 0; index < patternCount; ++index) {
                int reflected = reflectedIndex(
                    patternType, network.patternSize, index
                );
                patternValues[patternType][index] =
                    rawValues[index] + rawValues[reflected];
            }
        }
        valueIndex += 41; // 追加特徴用の公開モデル係数です。
        for (float& weight : patternWeights) {
            weight = values[valueIndex++];
        }
    }

    Score evaluate(const Board& board, char player) const {
        if (patternValues[0].empty()) {
            return 0;
        }
        constexpr std::array<std::array<int, 10>, 2> diagonals = {{
            {{0, 9, 18, 27, 36, 45, 54, 63, 0, 0}},
            {{7, 14, 21, 28, 35, 42, 49, 56, 0, 0}}
        }};
        constexpr std::array<std::array<int, 10>, 4> edges = {{
            {{9, 0, 1, 2, 3, 4, 5, 6, 7, 14}},
            {{49, 56, 57, 58, 59, 60, 61, 62, 63, 54}},
            {{9, 0, 8, 16, 24, 32, 40, 48, 56, 49}},
            {{14, 7, 15, 23, 31, 39, 47, 55, 63, 54}}
        }};
        constexpr std::array<std::array<int, 10>, 4> corners = {{
            {{0, 1, 2, 3, 8, 9, 10, 16, 17, 24}},
            {{7, 6, 5, 4, 15, 14, 13, 23, 22, 31}},
            {{56, 57, 58, 59, 48, 49, 50, 40, 41, 32}},
            {{63, 62, 61, 60, 55, 54, 53, 47, 46, 39}}
        }};

        float score = 0;
        for (const auto& positions : diagonals) {
            score += patternWeights[0]
                * patternValues[0][boardPatternIndex(board, positions, 8)];
        }
        for (const auto& positions : edges) {
            score += patternWeights[1]
                * patternValues[1][boardPatternIndex(board, positions, 10)];
        }
        for (const auto& positions : corners) {
            score += patternWeights[2]
                * patternValues[2][boardPatternIndex(board, positions, 10)];
        }
        return static_cast<Score>(score * (player == PLAYER_ZERO ? 1000 : -1000));
    }
};

const LearnedPatternEvaluator LEARNED_PATTERNS;

class Evaluator {
public:
    // 対応する角の占有状態を反映した位置価値を返します。
    Score getDynamicPositionWeight(
        const Board& board,
        int position
    ) const {
        struct CornerPositionCheck {
            int cornerPosition;
            std::array<int, 2> cPositions;
            int xPosition;
        };

        const std::array<CornerPositionCheck, 4> cornerChecks = {{
            {0, {{8, 1}}, 9},
            {7, {{15, 6}}, 14},
            {56, {{48, 57}}, 49},
            {63, {{55, 62}}, 54}
        }};

        for (const CornerPositionCheck& check : cornerChecks) {
            bool cornerIsOccupied = (
                board.occupiedBoard() & (1ULL << check.cornerPosition)
            ) != 0;

            for (int cPosition : check.cPositions) {
                if (position == cPosition) {
                    if (cornerIsOccupied) {
                        return OCCUPIED_CORNER_C_WEIGHT;
                    }
                    return EMPTY_CORNER_C_WEIGHT;
                }
            }

            if (position == check.xPosition) {
                if (cornerIsOccupied) {
                    return OCCUPIED_CORNER_X_WEIGHT;
                }
                return EMPTY_CORNER_X_WEIGHT;
            }
        }
        return POSITION_WEIGHTS[position];
    }

    // 盤面の各マスに設定した位置価値の合計差を評価します。
    Score evaluateBoardPosition(const Board& board, char player) const {
        Bitboard playerBits = board.playerBoard(player);
        Bitboard opponentBits = board.opponentBoard(player);
        Score score = 0;

        while (playerBits != 0) {
            Bitboard moveBit = playerBits & (~playerBits + 1ULL);
            playerBits -= moveBit;
            score += getDynamicPositionWeight(
                board,
                __builtin_ctzll(moveBit)
            );
        }

        while (opponentBits != 0) {
            Bitboard moveBit = opponentBits & (~opponentBits + 1ULL);
            opponentBits -= moveBit;
            score -= getDynamicPositionWeight(
                board,
                __builtin_ctzll(moveBit)
            );
        }
        return score;
    }

    // 合法な候補手が着手前盤面で辺ウェッジになっているか判定します。
    bool isEdgeWedgeMove(
        const Board& board,
        Bitboard moveBit,
        char player
    ) const {
        std::pair<int, int> position = bitToPosition(moveBit);
        int row = position.first;
        int column = position.second;
        bool isCorner = (row == 0 || row == BOARD_SIZE - 1)
            && (column == 0 || column == BOARD_SIZE - 1);

        if (isCorner) {
            return false;
        }

        Bitboard opponentBits = board.opponentBoard(player);
        if (row == 0 || row == BOARD_SIZE - 1) {
            Bitboard leftBit = bitMask(row, column - 1);
            Bitboard rightBit = bitMask(row, column + 1);
            return (opponentBits & leftBit) != 0
                && (opponentBits & rightBit) != 0;
        }
        if (column == 0 || column == BOARD_SIZE - 1) {
            Bitboard upperBit = bitMask(row - 1, column);
            Bitboard lowerBit = bitMask(row + 1, column);
            return (opponentBits & upperBit) != 0
                && (opponentBits & lowerBit) != 0;
        }
        return false;
    }

    // 指定した角から連続する同色石を数えます。
    int countConsecutiveStonesFromCorner(
        Bitboard playerBits,
        const std::array<int, 8>& ray
    ) const {
        int count = 0;
        for (int position : ray) {
            Bitboard positionBit = 1ULL << position;
            if ((playerBits & positionBit) == 0) {
                break;
            }
            ++count;
        }
        return count;
    }

    // 盤面変更なしで、候補手が角から続く複数空き区間に属するか判定します。
    bool collectDangerousRayCandidate(
        const Board& board,
        Bitboard moveBit,
        char player,
        const CornerRayReference& rayReference,
        DangerousRayCandidate& result
    ) const {
        if (rayReference.rayIndex < 0 || rayReference.rayIndex >= 8) {
            return false;
        }
        if (
            rayReference.positionInRay < 1
            || rayReference.positionInRay >= BOARD_SIZE - 1
        ) {
            return false;
        }

        const std::array<int, 8>& ray =
            CORNER_EDGE_RAYS[rayReference.rayIndex];
        int moveIndex = rayReference.positionInRay;
        Bitboard expectedMoveBit = 1ULL << ray[moveIndex];
        if (moveBit != expectedMoveBit) {
            return false;
        }

        Bitboard opponentBits = board.opponentBoard(player);
        Bitboard cornerBit = 1ULL << ray[0];
        if ((opponentBits & cornerBit) == 0) {
            return false;
        }
        Bitboard occupiedBits = board.occupiedBoard();
        if ((occupiedBits & moveBit) != 0) {
            return false;
        }

        int gapStart = moveIndex;
        Bitboard gapBits = moveBit;
        while (gapStart > 0) {
            Bitboard previousBit = 1ULL << ray[gapStart - 1];
            if ((occupiedBits & previousBit) != 0) {
                break;
            }
            gapBits |= previousBit;
            --gapStart;
        }

        int gapEnd = moveIndex;
        while (gapEnd < BOARD_SIZE - 1) {
            Bitboard nextBit = 1ULL << ray[gapEnd + 1];
            if ((occupiedBits & nextBit) != 0) {
                break;
            }
            gapBits |= nextBit;
            ++gapEnd;
        }

        if (gapEnd - gapStart + 1 < 2) {
            return false;
        }

        int opponentLineLength = countConsecutiveStonesFromCorner(
            opponentBits,
            ray
        );
        if (gapStart != opponentLineLength) {
            return false;
        }

        result = DangerousRayCandidate{
            rayReference.rayIndex,
            moveIndex,
            gapStart,
            gapEnd,
            gapBits
        };
        return true;
    }

    // moveBit must already be a legal move for player.
    bool isMultiEmptyEdgeGapMove(
        const Board& board,
        Bitboard moveBit,
        char player
    ) const {
        int movePosition = __builtin_ctzll(moveBit);
        const CornerRayReferences& rayReferences =
            CORNER_RAY_REFERENCES[movePosition];
        if (rayReferences.count == 0) {
            return false;
        }

        std::array<DangerousRayCandidate, 2> candidates = {};
        int candidateCount = 0;
        for (int index = 0; index < rayReferences.count; ++index) {
            DangerousRayCandidate candidate = {};
            if (collectDangerousRayCandidate(
                board,
                moveBit,
                player,
                rayReferences.references[index],
                candidate
            )) {
                candidates[candidateCount] = candidate;
                ++candidateCount;
            }
        }

        if (candidateCount == 0) {
            return false;
        }

        Board boardAfterMyMove = board.copy();
        boardAfterMyMove.applyMoveBit(moveBit, player);

        char opponent = opponentOf(player);
        Bitboard opponentLegalMoves =
            boardAfterMyMove.legalMovesBits(opponent);
        if (opponentLegalMoves == 0) {
            return false;
        }

        Bitboard opponentBitsAfterMyMove =
            boardAfterMyMove.playerBoard(opponent);
        Board replyBoard = boardAfterMyMove.copy();

        for (int index = 0; index < candidateCount; ++index) {
            const DangerousRayCandidate& candidate = candidates[index];
            const std::array<int, 8>& ray =
                CORNER_EDGE_RAYS[candidate.rayIndex];
            Bitboard remainingGapBits = candidate.gapBits & ~moveBit;
            Bitboard replyCandidates = opponentLegalMoves & remainingGapBits;
            if (replyCandidates == 0) {
                continue;
            }

            int lineLengthBeforeReply = countConsecutiveStonesFromCorner(
                opponentBitsAfterMyMove,
                ray
            );

            while (replyCandidates != 0) {
                Bitboard replyBit =
                    replyCandidates & (~replyCandidates + 1ULL);
                replyCandidates -= replyBit;

                MoveRecord replyRecord =
                    replyBoard.applyMoveBit(replyBit, opponent);
                int lineLengthAfterReply = countConsecutiveStonesFromCorner(
                    replyBoard.playerBoard(opponent),
                    ray
                );
                replyBoard.undoMove(replyRecord);

                if (lineLengthAfterReply > lineLengthBeforeReply) {
                    return true;
                }
            }
        }
        return false;
    }

    // 指定手の探索順スコアを着手前盤面から計算します。
    Score evaluateMoveScoreByBit(
        const Board& board,
        Bitboard moveBit,
        char player
    ) const {
        Score score = MOVE_WEIGHTS[__builtin_ctzll(moveBit)];
        if (isEdgeWedgeMove(board, moveBit, player)) {
            score += EDGE_WEDGE_MOVE_BONUS;
        }
        if (isMultiEmptyEdgeGapMove(board, moveBit, player)) {
            score += MULTI_EMPTY_EDGE_GAP_MOVE_PENALTY;
        }
        return score;
    }

    // 自分と相手のパス回数差を生値で返します。
    Score evaluatePass(int myPassCount, int opponentPassCount) const {
        return opponentPassCount - myPassCount;
    }

    // 自分と相手の石数差を生値で返します。
    Score evaluateStoneCount(const Board& board, char player) const {
        char opponent = opponentOf(player);
        return board.countStones(player) - board.countStones(opponent);
    }

    // 自分と相手の合法手数差を正規化して返します。
    Score evaluateMobility(const Board& board, char player) const {
        char opponent = opponentOf(player);
        int myMoves = __builtin_popcountll(board.legalMovesBits(player));
        int opponentMoves = __builtin_popcountll(board.legalMovesBits(opponent));
        return normalizedDifference(myMoves, opponentMoves);
    }

    // 潜在的な着手可能性の差を正規化して返します。
    Score evaluatePotentialMobility(const Board& board, char player) const {
        Bitboard playerBits = board.playerBoard(player);
        Bitboard opponentBits = board.opponentBoard(player);
        Bitboard emptyBits = ~board.occupiedBoard();

        int myPotential = __builtin_popcountll(neighborBits(opponentBits) & emptyBits);
        int opponentPotential = __builtin_popcountll(neighborBits(playerBits) & emptyBits);
        return normalizedDifference(myPotential, opponentPotential);
    }

    // 少ないほど有利なフロンティア石数の差を正規化して返します。
    Score evaluateFrontier(const Board& board, char player) const {
        Bitboard playerBits = board.playerBoard(player);
        Bitboard opponentBits = board.opponentBoard(player);
        Bitboard emptyNeighbors = neighborBits(~board.occupiedBoard());

        int myFrontier = __builtin_popcountll(playerBits & emptyNeighbors);
        int opponentFrontier = __builtin_popcountll(opponentBits & emptyNeighbors);
        return normalizedDifference(opponentFrontier, myFrontier);
    }

    // 角から指定方向へ連続している同色石を数えます。
    int countStableLineFromCorner(
        Bitboard ownerBits,
        Bitboard startBit,
        ShiftFunction shiftFunction
    ) const {
        int stableCount = 0;
        Bitboard currentBit = startBit;

        while (currentBit != 0 && (currentBit & ownerBits) != 0) {
            ++stableCount;
            currentBit = shiftFunction(currentBit);
        }
        return stableCount;
    }

    // 角と角から続く辺の確定石近似数の差を生値で返します。
    Score evaluateStability(const Board& board, char player) const {
        Bitboard playerBits = board.playerBoard(player);
        Bitboard opponentBits = board.opponentBoard(player);
        int myStable = 0;
        int opponentStable = 0;

        struct CornerCheck {
            Bitboard cornerBit;
            ShiftFunction horizontalShift;
            ShiftFunction verticalShift;
        };

        const std::array<CornerCheck, 4> cornerChecks = {{
            {bitMask(0, 0), shiftEast, shiftSouth},
            {bitMask(0, 7), shiftWest, shiftSouth},
            {bitMask(7, 0), shiftEast, shiftNorth},
            {bitMask(7, 7), shiftWest, shiftNorth}
        }};

        for (const CornerCheck& check : cornerChecks) {
            Bitboard ownerBits = 0;
            bool isMyCorner = (check.cornerBit & playerBits) != 0;
            bool isOpponentCorner = (check.cornerBit & opponentBits) != 0;

            if (isMyCorner) {
                ownerBits = playerBits;
            } else if (isOpponentCorner) {
                ownerBits = opponentBits;
            } else {
                continue;
            }

            int cornerStable = 1;
            cornerStable += countStableLineFromCorner(
                ownerBits,
                check.horizontalShift(check.cornerBit),
                check.horizontalShift
            );
            cornerStable += countStableLineFromCorner(
                ownerBits,
                check.verticalShift(check.cornerBit),
                check.verticalShift
            );

            if (isMyCorner) {
                myStable += cornerStable;
            } else {
                opponentStable += cornerStable;
            }
        }
        return myStable - opponentStable;
    }

    // 4辺にある「相手・自分・相手」の中央石を盤面だけから評価します。
    Score evaluateEdgeWedges(const Board& board, char player) const {
        Bitboard playerBits = board.playerBoard(player);
        Bitboard opponentBits = board.opponentBoard(player);
        Score score = 0;

        for (const std::array<int, 8>& edge : EDGE_POSITIONS) {
            for (int index = 1; index < BOARD_SIZE - 1; ++index) {
                Bitboard previousBit = 1ULL << edge[index - 1];
                Bitboard centerBit = 1ULL << edge[index];
                Bitboard nextBit = 1ULL << edge[index + 1];

                if (
                    (playerBits & centerBit) != 0
                    && (opponentBits & previousBit) != 0
                    && (opponentBits & nextBit) != 0
                ) {
                    score += EDGE_WEDGE_POSITION_BONUS;
                } else if (
                    (opponentBits & centerBit) != 0
                    && (playerBits & previousBit) != 0
                    && (playerBits & nextBit) != 0
                ) {
                    score -= EDGE_WEDGE_POSITION_BONUS;
                }
            }
        }
        return score;
    }

    // 角から斜めに続く石の並びを、角周辺パターンとして評価します。
    Score evaluateCornerDiagonalPattern(const Board& board, char player) const {
        constexpr std::array<std::array<int, 4>, 4> diagonalRays = {{
            {{0, 9, 18, 27}},
            {{7, 14, 21, 28}},
            {{56, 49, 42, 35}},
            {{63, 54, 45, 36}}
        }};

        Bitboard playerBits = board.playerBoard(player);
        Bitboard opponentBits = board.opponentBoard(player);
        Score score = 0;

        for (const std::array<int, 4>& ray : diagonalRays) {
            Bitboard cornerBit = 1ULL << ray[0];
            Bitboard ownerBits = 0;
            Score direction = 0;

            if ((playerBits & cornerBit) != 0) {
                ownerBits = playerBits;
                direction = 1;
            } else if ((opponentBits & cornerBit) != 0) {
                ownerBits = opponentBits;
                direction = -1;
            } else {
                continue;
            }

            for (int index = 1; index < static_cast<int>(ray.size()); ++index) {
                if ((ownerBits & (1ULL << ray[index])) == 0) {
                    break;
                }
                score += direction;
            }
        }
        return score;
    }

    // 各生評価へ定数倍率を適用して総合評価値を返します。
    Score evaluateAll(
        const Board& board,
        char player,
        int myPassCount,
        int opponentPassCount
    ) const {
        Score score = 0;
        score += evaluateBoardPosition(board, player)
            * POSITION_EVALUATION_WEIGHT;
        score += evaluateStoneCount(board, player)
            * STONE_DIFFERENCE_WEIGHT;
        score += evaluateMobility(board, player)
            * MOBILITY_WEIGHT;
        score += evaluatePotentialMobility(board, player)
            * POTENTIAL_MOBILITY_WEIGHT;
        score += evaluateFrontier(board, player)
            * FRONTIER_WEIGHT;
        score += evaluateStability(board, player)
            * STABILITY_WEIGHT;
        score += evaluatePass(myPassCount, opponentPassCount)
            * PASS_WEIGHT;
        score += evaluateEdgeWedges(board, player);
        int emptyCount = board.countEmptyCells();
        Score patternWeight = emptyCount > 40 ? 1 : (emptyCount > 20 ? 2 : 4);
        score += evaluateCornerDiagonalPattern(board, player)
            * CORNER_DIAGONAL_PATTERN_WEIGHT
            * patternWeight;
        score += LEARNED_PATTERNS.evaluate(board, player);
        return score;
    }
};

enum class BoundType {
    EXACT,
    LOWER_BOUND,
    UPPER_BOUND
};

struct PerfectCacheKey {
    Bitboard blackBoard;
    Bitboard whiteBoard;
    char currentPlayer;
    char rootPlayer;

    // キャッシュキー同士の一致判定を行います。
    bool operator==(const PerfectCacheKey& other) const {
        return blackBoard == other.blackBoard
            && whiteBoard == other.whiteBoard
            && currentPlayer == other.currentPlayer
            && rootPlayer == other.rootPlayer;
    }
};

struct PerfectCacheEntry {
    Score score;
    Bitboard bestMoveBit;
    BoundType boundType;
};

struct TranspositionKey {
    Bitboard blackBoard;
    Bitboard whiteBoard;
    char currentPlayer;
    char rootPlayer;
    int myPassCount;
    int opponentPassCount;

    // 置換表キー同士の一致判定を行います。
    bool operator==(const TranspositionKey& other) const {
        return blackBoard == other.blackBoard
            && whiteBoard == other.whiteBoard
            && currentPlayer == other.currentPlayer
            && rootPlayer == other.rootPlayer
            && myPassCount == other.myPassCount
            && opponentPassCount == other.opponentPassCount;
    }
};

struct TranspositionKeyHash {
    // 通常探索用置換表キーのハッシュ値を生成します。
    std::size_t operator()(const TranspositionKey& key) const {
        std::size_t hashValue = std::hash<Bitboard>{}(key.blackBoard);
        hashValue ^= std::hash<Bitboard>{}(key.whiteBoard) + 0x9e3779b9 + (hashValue << 6) + (hashValue >> 2);
        hashValue ^= std::hash<int>{}(key.currentPlayer) + 0x9e3779b9 + (hashValue << 6) + (hashValue >> 2);
        hashValue ^= std::hash<int>{}(key.rootPlayer) + 0x9e3779b9 + (hashValue << 6) + (hashValue >> 2);
        hashValue ^= std::hash<int>{}(key.myPassCount) + 0x9e3779b9 + (hashValue << 6) + (hashValue >> 2);
        hashValue ^= std::hash<int>{}(key.opponentPassCount) + 0x9e3779b9 + (hashValue << 6) + (hashValue >> 2);
        return hashValue;
    }
};

struct TranspositionEntry {
    int searchedDepth;
    Score score;
    Bitboard bestMoveBit;
    BoundType boundType;
};

struct RootSearchResult {
    Score score;
    Bitboard bestMoveBit;
    bool hasBestMove;
    int completedMoveCount;
};

struct PerfectCacheKeyHash {
    // 終盤完全読みキャッシュ用キーのハッシュ値を生成します。
    std::size_t operator()(const PerfectCacheKey& key) const {
        std::size_t hashValue = std::hash<Bitboard>{}(key.blackBoard);
        hashValue ^= std::hash<Bitboard>{}(key.whiteBoard) + 0x9e3779b9 + (hashValue << 6) + (hashValue >> 2);
        hashValue ^= std::hash<int>{}(key.currentPlayer) + 0x9e3779b9 + (hashValue << 6) + (hashValue >> 2);
        hashValue ^= std::hash<int>{}(key.rootPlayer) + 0x9e3779b9 + (hashValue << 6) + (hashValue >> 2);
        return hashValue;
    }
};

class MiniMaxOthelloAi {
private:
    char player;
    Evaluator evaluator;
    Clock::time_point endTime;
    bool timedOut;
    bool allowPerfectSearch;
    std::uint32_t nodesUntilTimeCheck;
    std::unordered_map<PerfectCacheKey, PerfectCacheEntry, PerfectCacheKeyHash> perfectCache;
    std::unordered_map<TranspositionKey, TranspositionEntry, TranspositionKeyHash> transpositionTable;

public:
    // AIが担当するプレイヤーを設定して探索器を初期化します。
    explicit MiniMaxOthelloAi(int playerId)
        : player(static_cast<char>('0' + playerId)),
          timedOut(false),
          allowPerfectSearch(true),
          nodesUntilTimeCheck(0) {
        transpositionTable.reserve(131072);
        perfectCache.reserve(131072);
    }

    // 現在時刻が探索終了時刻へ到達したか判定します。
    bool isTimeUp() const {
        return Clock::now() >= endTime;
    }

    // 再帰探索では時刻取得を一定ノード間隔に抑えます。
    bool isSearchTimeUp() {
        if (nodesUntilTimeCheck > 0) {
            --nodesUntilTimeCheck;
            return false;
        }
        nodesUntilTimeCheck = TIME_CHECK_NODE_INTERVAL - 1;
        return isTimeUp();
    }

    // 保存済み最善手を先頭にし、残りを探索順スコアの高い順へ並べ替えます。
    void sortMoves(
        Board& board,
        char currentPlayer,
        std::vector<Bitboard>& moveBits,
        Bitboard preferredMoveBit = 0,
        bool useEmptyRegionParity = false,
        bool useTacticalOrdering = false
    ) const {
        std::vector<std::pair<Bitboard, Score>> scoredMoves;
        scoredMoves.reserve(moveBits.size());
        for (Bitboard moveBit : moveBits) {
            Score moveScore = evaluator.evaluateMoveScoreByBit(
                board,
                moveBit,
                currentPlayer
            );
            if (useEmptyRegionParity && belongsToOddQuadrant(board, moveBit)) {
                moveScore += ODD_EMPTY_REGION_MOVE_ORDER_BONUS;
            }
            if (useTacticalOrdering) {
                MoveRecord moveRecord = board.applyMoveBit(moveBit, currentPlayer);
                Bitboard opponentMoves = board.legalMovesBits(opponentOf(currentPlayer));
                int opponentMoveCount = __builtin_popcountll(opponentMoves);
                int opponentPotentialCount = __builtin_popcountll(
                    neighborBits(board.playerBoard(currentPlayer)) & ~board.occupiedBoard()
                );
                int opponentCornerCount = __builtin_popcountll(
                    opponentMoves & (1ULL | (1ULL << 7) | (1ULL << 56) | (1ULL << 63))
                );
                board.undoMove(moveRecord);

                moveScore -= opponentMoveCount * OPPONENT_MOVE_ORDER_PENALTY;
                moveScore -= opponentPotentialCount
                    * OPPONENT_POTENTIAL_MOVE_ORDER_PENALTY;
                if (opponentMoveCount == 0) {
                    moveScore += OPPONENT_PASS_MOVE_ORDER_BONUS;
                }
                moveScore -= opponentCornerCount * OPPONENT_CORNER_MOVE_ORDER_PENALTY;
            }
            scoredMoves.push_back({moveBit, moveScore});
        }

        std::sort(
            scoredMoves.begin(),
            scoredMoves.end(),
            [](const std::pair<Bitboard, Score>& leftMove,
               const std::pair<Bitboard, Score>& rightMove) {
                return leftMove.second > rightMove.second;
            }
        );

        moveBits.clear();
        for (const std::pair<Bitboard, Score>& scoredMove : scoredMoves) {
            moveBits.push_back(scoredMove.first);
        }

        auto preferredIterator = std::find(
            moveBits.begin(),
            moveBits.end(),
            preferredMoveBit
        );
        if (preferredIterator != moveBits.end()) {
            std::rotate(moveBits.begin(), preferredIterator, preferredIterator + 1);
        }
    }

    // 着手位置が属する4×4区画の空きマス数が奇数か判定します。
    bool belongsToOddQuadrant(const Board& board, Bitboard moveBit) const {
        Bitboard emptyBits = ~board.occupiedBoard();
        int position = __builtin_ctzll(moveBit);
        int firstRow = (position / BOARD_SIZE) < 4 ? 0 : 4;
        int firstColumn = (position % BOARD_SIZE) < 4 ? 0 : 4;
        Bitboard quadrantBits = 0;
        for (int row = firstRow; row < firstRow + 4; ++row) {
            for (int column = firstColumn; column < firstColumn + 4; ++column) {
                quadrantBits |= bitMask(row, column);
            }
        }
        return (__builtin_popcountll(emptyBits & quadrantBits) % 2) == 1;
    }

    // 合法手から、定められた制限時間内で最善手を選択します。
    std::string chooseMove(Board& board, const std::vector<std::string>& legalActions) {
        if (legalActions.empty()) {
            return "PASS";
        }

        std::vector<Bitboard> legalMoveBits;
        legalMoveBits.reserve(legalActions.size());
        for (const std::string& action : legalActions) {
            legalMoveBits.push_back(actionToBit(action));
        }

        int placedCount = board.countStones(PLAYER_ZERO) + board.countStones(PLAYER_ONE);
        int emptyCount = board.countEmptyCells();
        double timeLimitSeconds = 0.12;

        if (placedCount <= 5) {
            timeLimitSeconds = 1.75;
        }

        endTime = Clock::now() + std::chrono::duration_cast<Clock::duration>(
            std::chrono::duration<double>(timeLimitSeconds)
        );
        const Clock::time_point fullEndTime = endTime;
        timedOut = false;
        nodesUntilTimeCheck = 0;

        // 前の手番で作成した有効なキャッシュは再利用し、肥大化した場合だけ破棄します。
        if (transpositionTable.size() >= MAX_TRANSPOSITION_ENTRIES) {
            transpositionTable.clear();
        }
        if (perfectCache.size() >= MAX_PERFECT_CACHE_ENTRIES) {
            perfectCache.clear();
        }

        sortMoves(board, player, legalMoveBits, 0, false, true);

        // 深さ1も読み切れない場合に備え、まず全合法手を静的評価します。
        Bitboard bestMoveBit = legalMoveBits.front();
        Score bestFallbackScore = -INF;

        for (Bitboard moveBit : legalMoveBits) {
            MoveRecord moveRecord = board.applyMoveBit(moveBit, player);
            Score score = evaluator.evaluateAll(board, player, 0, 0);
            board.undoMove(moveRecord);

            if (score > bestFallbackScore) {
                bestFallbackScore = score;
                bestMoveBit = moveBit;
            }
        }

        // 終盤では終局まで読み、時間切れ時は完了済み候補の中の最善手を返します。
        if (emptyCount <= PERFECT_EMPTY_LIMIT) {
            // まず全体時間の30%を通常探索に使い、時間切れ時の候補手を確保します。
            Clock::duration totalBudget = std::chrono::duration_cast<Clock::duration>(
                std::chrono::duration<double>(timeLimitSeconds)
            );
            Clock::time_point searchStartTime = fullEndTime - totalBudget;
            endTime = searchStartTime + totalBudget * 3 / 10;
            allowPerfectSearch = false;
            for (int depth = 1; depth <= emptyCount && !isTimeUp(); ++depth) {
                RootSearchResult result = searchRoot(
                    board,
                    legalMoveBits,
                    depth,
                    -INF,
                    INF,
                    bestMoveBit
                );
                if (timedOut) {
                    break;
                }
                if (result.hasBestMove) {
                    bestMoveBit = result.bestMoveBit;
                    debug("endgame fallback depth=" + std::to_string(depth));
                }
            }

            allowPerfectSearch = true;
            timedOut = false;
            nodesUntilTimeCheck = 0;
            endTime = fullEndTime;

            Score currentBestScore = -INF;
            Score alpha = -INF;
            Score beta = INF;
            int completedMoveCount = 0;

            for (Bitboard moveBit : legalMoveBits) {
                if (isTimeUp()) {
                    timedOut = true;
                    break;
                }

                MoveRecord moveRecord = board.applyMoveBit(moveBit, player);
                Score score = perfectPlay(board, opponentOf(player), player, alpha, beta);
                board.undoMove(moveRecord);

                if (timedOut) {
                    break;
                }

                ++completedMoveCount;
                if (score > currentBestScore) {
                    currentBestScore = score;
                    bestMoveBit = moveBit;
                }
                if (score > alpha) {
                    alpha = score;
                }
            }

            if (timedOut) {
                debug(
                    "perfect timeout completed_root_moves="
                    + std::to_string(completedMoveCount)
                    + " best=" + bitToAction(bestMoveBit)
                );
                return bitToAction(bestMoveBit);
            }

            debug("perfect score=" + std::to_string(currentBestScore));
            return bitToAction(bestMoveBit);
        }

        int depth = 1;
        const int maxDepth = std::min(64, std::max(1, emptyCount - PERFECT_EMPTY_LIMIT));
        Bitboard lastCompletedBestMoveBit = bestMoveBit;
        Score lastCompletedBestScore = 0;
        bool hasLastCompletedScore = false;

        // 1手の評価で使う既存MOVE_WEIGHTSの最大値を初期窓の半幅にします。
        const Score initialAspirationWidth = static_cast<Score>(
            *std::max_element(MOVE_WEIGHTS.begin(), MOVE_WEIGHTS.end())
        );

        while (depth <= maxDepth) {
            if (isTimeUp()) {
                break;
            }

            Score alpha = -INF;
            Score beta = INF;
            Score aspirationWidth = initialAspirationWidth;
            int aspirationFailureCount = 0;
            Bitboard preferredMoveBit = lastCompletedBestMoveBit;

            if (depth >= 2 && hasLastCompletedScore) {
                alpha = std::max(-INF, lastCompletedBestScore - aspirationWidth);
                beta = std::min(INF, lastCompletedBestScore + aspirationWidth);
            }

            while (!isTimeUp()) {
                RootSearchResult result = searchRoot(
                    board,
                    legalMoveBits,
                    depth,
                    alpha,
                    beta,
                    preferredMoveBit
                );

                // 探索途中の候補は、次の探索の手順並べ替えにだけ使います。
                if (result.hasBestMove) {
                    preferredMoveBit = result.bestMoveBit;
                }

                if (timedOut) {
                    debug(
                        "timeout depth=" + std::to_string(depth)
                        + " completed_root_moves=" + std::to_string(result.completedMoveCount)
                        + " best=" + bitToAction(bestMoveBit)
                    );
                    break;
                }

                if (!result.hasBestMove) {
                    break;
                }

                bool failedLow = alpha > -INF && result.score <= alpha;
                bool failedHigh = beta < INF && result.score >= beta;

                if (failedLow || failedHigh) {
                    ++aspirationFailureCount;
                    if (aspirationFailureCount >= 3) {
                        alpha = -INF;
                        beta = INF;
                    } else {
                        aspirationWidth *= 2;
                        alpha = std::max(-INF, lastCompletedBestScore - aspirationWidth);
                        beta = std::min(INF, lastCompletedBestScore + aspirationWidth);
                    }
                    continue;
                }

                lastCompletedBestMoveBit = result.bestMoveBit;
                bestMoveBit = lastCompletedBestMoveBit;
                lastCompletedBestScore = result.score;
                hasLastCompletedScore = true;
                debug("depth=" + std::to_string(depth) + " score=" + std::to_string(result.score));
                ++depth;
                break;
            }

            if (timedOut || isTimeUp()) {
                break;
            }
        }
        return bitToAction(bestMoveBit);
    }

private:
    // ルート局面をPVSで探索し、完了済み候補の最善手も含めて返します。
    RootSearchResult searchRoot(
        Board& board,
        const std::vector<Bitboard>& legalMoveBits,
        int depth,
        Score alpha,
        Score beta,
        Bitboard preferredMoveBit
    ) {
        std::vector<Bitboard> orderedMoveBits = legalMoveBits;
        sortMoves(board, player, orderedMoveBits, preferredMoveBit, false, true);

        Score bestScore = -INF;
        Bitboard bestMoveBit = 0;
        bool hasBestMove = false;
        bool isFirstMove = true;
        int completedMoveCount = 0;

        for (Bitboard moveBit : orderedMoveBits) {
            if (isTimeUp()) {
                timedOut = true;
                break;
            }

            MoveRecord moveRecord = board.applyMoveBit(moveBit, player);
            Score score;

            if (isFirstMove) {
                score = minimax(
                    board,
                    depth - 1,
                    alpha,
                    beta,
                    opponentOf(player),
                    player,
                    0,
                    0
                );
            } else {
                score = minimax(
                    board,
                    depth - 1,
                    alpha,
                    alpha + 1,
                    opponentOf(player),
                    player,
                    0,
                    0
                );

                if (!timedOut && score > alpha && score < beta) {
                    score = minimax(
                        board,
                        depth - 1,
                        alpha,
                        beta,
                        opponentOf(player),
                        player,
                        0,
                        0
                    );
                }
            }
            board.undoMove(moveRecord);

            // 時間切れになった候補は未完了なので、最善手候補へ含めません。
            if (timedOut) {
                break;
            }

            ++completedMoveCount;
            if (!hasBestMove || score > bestScore) {
                bestScore = score;
                bestMoveBit = moveBit;
                hasBestMove = true;
            }
            if (score > alpha) {
                alpha = score;
            }
            if (beta <= alpha) {
                break;
            }
            isFirstMove = false;
        }

        return RootSearchResult{
            bestScore,
            bestMoveBit,
            hasBestMove,
            completedMoveCount
        };
    }

    // 探索窓と評価値から置換表へ保存する境界種別を判定します。
    BoundType determineBoundType(Score score, Score originalAlpha, Score originalBeta) const {
        if (score <= originalAlpha) {
            return BoundType::UPPER_BOUND;
        }
        if (score >= originalBeta) {
            return BoundType::LOWER_BOUND;
        }
        return BoundType::EXACT;
    }

    // 通常探索の完了結果を、深度と境界種別を保って置換表へ保存します。
    void storeTransposition(
        const TranspositionKey& cacheKey,
        int depth,
        Score score,
        Bitboard bestMoveBit,
        Score originalAlpha,
        Score originalBeta
    ) {
        if (timedOut) {
            return;
        }

        BoundType boundType = determineBoundType(score, originalAlpha, originalBeta);
        auto cacheIterator = transpositionTable.find(cacheKey);

        if (cacheIterator == transpositionTable.end()
            && transpositionTable.size() >= MAX_TRANSPOSITION_ENTRIES) {
            return;
        }

        bool shouldReplace = cacheIterator == transpositionTable.end()
            || depth > cacheIterator->second.searchedDepth
            || (depth == cacheIterator->second.searchedDepth
                && (cacheIterator->second.boundType != BoundType::EXACT
                    || boundType == BoundType::EXACT));

        if (shouldReplace) {
            TranspositionEntry entry{
                depth,
                score,
                bestMoveBit,
                boundType
            };
            if (cacheIterator == transpositionTable.end()) {
                transpositionTable.emplace(cacheKey, entry);
            } else {
                cacheIterator->second = entry;
            }
        }
    }

    // 深さ到達または終局で確定した評価値を置換表へ保存します。
    void storeExactTransposition(
        const TranspositionKey& cacheKey,
        int depth,
        Score score
    ) {
        if (timedOut) {
            return;
        }

        auto cacheIterator = transpositionTable.find(cacheKey);

        if (cacheIterator == transpositionTable.end()
            && transpositionTable.size() >= MAX_TRANSPOSITION_ENTRIES) {
            return;
        }

        if (cacheIterator == transpositionTable.end()
            || depth >= cacheIterator->second.searchedDepth) {
            TranspositionEntry entry{
                depth,
                score,
                0,
                BoundType::EXACT
            };
            if (cacheIterator == transpositionTable.end()) {
                transpositionTable.emplace(cacheKey, entry);
            } else {
                cacheIterator->second = entry;
            }
        }
    }

    // 完全読みの完了結果を境界種別付きでキャッシュへ保存します。
    void storePerfectCache(
        const PerfectCacheKey& cacheKey,
        Score score,
        Bitboard bestMoveBit,
        Score originalAlpha,
        Score originalBeta
    ) {
        if (timedOut) {
            return;
        }

        BoundType boundType = determineBoundType(score, originalAlpha, originalBeta);
        auto cacheIterator = perfectCache.find(cacheKey);

        if (cacheIterator == perfectCache.end()
            && perfectCache.size() >= MAX_PERFECT_CACHE_ENTRIES) {
            return;
        }

        if (cacheIterator == perfectCache.end()
            || cacheIterator->second.boundType != BoundType::EXACT
            || boundType == BoundType::EXACT) {
            PerfectCacheEntry entry{score, bestMoveBit, boundType};
            if (cacheIterator == perfectCache.end()) {
                perfectCache.emplace(cacheKey, entry);
            } else {
                cacheIterator->second = entry;
            }
        }
    }

    // 深さ制限付きMinimax探索をPVSとalpha-beta枝刈りで実行します。
    Score minimax(
        Board& board,
        int depth,
        Score alpha,
        Score beta,
        char currentPlayer,
        char rootPlayer,
        int myPassCount,
        int opponentPassCount
    ) {
        if (isSearchTimeUp()) {
            timedOut = true;
            return evaluator.evaluateAll(
                board,
                rootPlayer,
                myPassCount,
                opponentPassCount
            );
        }

        int emptyCount = board.countEmptyCells();
        if (emptyCount <= PERFECT_EMPTY_LIMIT && allowPerfectSearch) {
            return perfectPlay(board, currentPlayer, rootPlayer, alpha, beta);
        }

        Score originalAlpha = alpha;
        Score originalBeta = beta;
        TranspositionKey cacheKey{
            board.blackBoard,
            board.whiteBoard,
            currentPlayer,
            rootPlayer,
            myPassCount,
            opponentPassCount
        };
        auto cacheIterator = transpositionTable.find(cacheKey);
        Bitboard preferredMoveBit = 0;

        if (cacheIterator != transpositionTable.end()) {
            const TranspositionEntry& entry = cacheIterator->second;
            preferredMoveBit = entry.bestMoveBit;

            if (entry.searchedDepth >= depth) {
                if (entry.boundType == BoundType::EXACT) {
                    return entry.score;
                }
                if (entry.boundType == BoundType::LOWER_BOUND) {
                    alpha = std::max(alpha, entry.score);
                } else {
                    beta = std::min(beta, entry.score);
                }
                if (beta <= alpha) {
                    return entry.score;
                }
            }
        }

        if (depth <= 0) {
            Score score = evaluator.evaluateAll(
                board,
                rootPlayer,
                myPassCount,
                opponentPassCount
            );
            storeExactTransposition(cacheKey, depth, score);
            return score;
        }

        std::vector<Bitboard> legalMoveBits = board.getLegalMoveBitsList(currentPlayer);
        char opponentPlayer = opponentOf(currentPlayer);

        if (legalMoveBits.empty()) {
            Bitboard opponentMovesBits = board.legalMovesBits(opponentPlayer);

            if (opponentMovesBits == 0) {
                Score score = finalScore(board, rootPlayer);
                storeExactTransposition(cacheKey, depth, score);
                return score;
            }

            Score score;
            if (currentPlayer == rootPlayer) {
                score = minimax(
                    board,
                    depth - 1,
                    alpha,
                    beta,
                    opponentPlayer,
                    rootPlayer,
                    myPassCount + 1,
                    opponentPassCount
                );
            } else {
                score = minimax(
                    board,
                    depth - 1,
                    alpha,
                    beta,
                    opponentPlayer,
                    rootPlayer,
                    myPassCount,
                    opponentPassCount + 1
                );
            }
            storeTransposition(cacheKey, depth, score, 0, originalAlpha, originalBeta);
            return score;
        }

        sortMoves(
            board,
            currentPlayer,
            legalMoveBits,
            preferredMoveBit,
            false,
            depth >= 4
        );

        if (currentPlayer == rootPlayer) {
            Score bestScore = -INF;
            Bitboard bestMoveBit = 0;
            bool isFirstMove = true;

            for (Bitboard moveBit : legalMoveBits) {
                MoveRecord moveRecord = board.applyMoveBit(moveBit, currentPlayer);
                Score score;

                if (isFirstMove) {
                    score = minimax(
                        board,
                        depth - 1,
                        alpha,
                        beta,
                        opponentPlayer,
                        rootPlayer,
                        myPassCount,
                        opponentPassCount
                    );
                } else {
                    score = minimax(
                        board,
                        depth - 1,
                        alpha,
                        alpha + 1,
                        opponentPlayer,
                        rootPlayer,
                        myPassCount,
                        opponentPassCount
                    );

                    if (!timedOut && score > alpha && score < beta) {
                        score = minimax(
                            board,
                            depth - 1,
                            alpha,
                            beta,
                            opponentPlayer,
                            rootPlayer,
                            myPassCount,
                            opponentPassCount
                        );
                    }
                }
                board.undoMove(moveRecord);

                if (timedOut) {
                    break;
                }

                if (score > bestScore) {
                    bestScore = score;
                    bestMoveBit = moveBit;
                }
                if (score > alpha) {
                    alpha = score;
                }
                if (beta <= alpha) {
                    break;
                }
                isFirstMove = false;
            }
            storeTransposition(
                cacheKey,
                depth,
                bestScore,
                bestMoveBit,
                originalAlpha,
                originalBeta
            );
            return bestScore;
        }

        Score bestScore = INF;
        Bitboard bestMoveBit = 0;
        bool isFirstMove = true;

        for (Bitboard moveBit : legalMoveBits) {
            MoveRecord moveRecord = board.applyMoveBit(moveBit, currentPlayer);
            Score score;

            if (isFirstMove) {
                score = minimax(
                    board,
                    depth - 1,
                    alpha,
                    beta,
                    opponentPlayer,
                    rootPlayer,
                    myPassCount,
                    opponentPassCount
                );
            } else {
                score = minimax(
                    board,
                    depth - 1,
                    beta - 1,
                    beta,
                    opponentPlayer,
                    rootPlayer,
                    myPassCount,
                    opponentPassCount
                );

                if (!timedOut && score < beta && score > alpha) {
                    score = minimax(
                        board,
                        depth - 1,
                        alpha,
                        beta,
                        opponentPlayer,
                        rootPlayer,
                        myPassCount,
                        opponentPassCount
                    );
                }
            }
            board.undoMove(moveRecord);

            if (timedOut) {
                break;
            }

            if (score < bestScore) {
                bestScore = score;
                bestMoveBit = moveBit;
            }
            if (score < beta) {
                beta = score;
            }
            if (beta <= alpha) {
                break;
            }
            isFirstMove = false;
        }

        storeTransposition(
            cacheKey,
            depth,
            bestScore,
            bestMoveBit,
            originalAlpha,
            originalBeta
        );
        return bestScore;
    }

    // 角から辺へ連続する、確実に反転しない石を重複なしで数えます。
    int countStableEdgeStones(const Board& board, char targetPlayer) const {
        Bitboard playerBits = board.playerBoard(targetPlayer);
        Bitboard stableBits = 0;
        for (const std::array<int, 8>& ray : CORNER_EDGE_RAYS) {
            if ((playerBits & (1ULL << ray[0])) == 0) {
                continue;
            }
            for (int position : ray) {
                Bitboard positionBit = 1ULL << position;
                if ((playerBits & positionBit) == 0) {
                    break;
                }
                stableBits |= positionBit;
            }
        }
        return __builtin_popcountll(stableBits);
    }

    // 空きマスが少ない終盤をゲーム終了まで完全探索します。
    Score perfectPlay(
        Board& board,
        char currentPlayer,
        char rootPlayer,
        Score alpha,
        Score beta
    ) {
        if (isSearchTimeUp()) {
            timedOut = true;
            return finalScore(board, rootPlayer);
        }

        Score originalAlpha = alpha;
        Score originalBeta = beta;

        PerfectCacheKey cacheKey{
            board.blackBoard,
            board.whiteBoard,
            currentPlayer,
            rootPlayer
        };
        auto cacheIterator = perfectCache.find(cacheKey);

        if (cacheIterator != perfectCache.end()) {
            const PerfectCacheEntry& entry = cacheIterator->second;

            if (entry.boundType == BoundType::EXACT) {
                return entry.score;
            }
            if (entry.boundType == BoundType::LOWER_BOUND) {
                alpha = std::max(alpha, entry.score);
            } else {
                beta = std::min(beta, entry.score);
            }
            if (beta <= alpha) {
                return entry.score;
            }
        }

        if (board.countEmptyCells() >= 5) {
            int myStableCount = countStableEdgeStones(board, rootPlayer);
            int opponentStableCount = countStableEdgeStones(board, opponentOf(rootPlayer));
            Score lowerBound = static_cast<Score>(2 * myStableCount - 64) * 100000;
            Score upperBound = static_cast<Score>(64 - 2 * opponentStableCount) * 100000;
            if (lowerBound >= beta) {
                return lowerBound;
            }
            if (upperBound <= alpha) {
                return upperBound;
            }
            alpha = std::max(alpha, lowerBound);
            beta = std::min(beta, upperBound);
        }

        std::vector<Bitboard> legalMoveBits = board.getLegalMoveBitsList(currentPlayer);
        char opponentPlayer = opponentOf(currentPlayer);

        if (legalMoveBits.empty()) {
            Bitboard opponentMovesBits = board.legalMovesBits(opponentPlayer);

            if (opponentMovesBits == 0) {
                Score score = finalScore(board, rootPlayer);
                perfectCache[cacheKey] = PerfectCacheEntry{
                    score,
                    0,
                    BoundType::EXACT
                };
                return score;
            }

            Score score = perfectPlay(board, opponentPlayer, rootPlayer, alpha, beta);
            storePerfectCache(cacheKey, score, 0, originalAlpha, originalBeta);
            return score;
        }

        Bitboard preferredMoveBit = 0;
        if (cacheIterator != perfectCache.end()) {
            preferredMoveBit = cacheIterator->second.bestMoveBit;
        }
        sortMoves(board, currentPlayer, legalMoveBits, preferredMoveBit, true, true);

        if (currentPlayer == rootPlayer) {
            Score bestScore = -INF;
            Bitboard bestMoveBit = 0;

            for (Bitboard moveBit : legalMoveBits) {
                MoveRecord moveRecord = board.applyMoveBit(moveBit, currentPlayer);
                Score score = perfectPlay(board, opponentPlayer, rootPlayer, alpha, beta);
                board.undoMove(moveRecord);

                if (timedOut) {
                    break;
                }

                if (score > bestScore) {
                    bestScore = score;
                    bestMoveBit = moveBit;
                }
                if (score > alpha) {
                    alpha = score;
                }
                if (beta <= alpha) {
                    break;
                }
            }

            storePerfectCache(
                cacheKey,
                bestScore,
                bestMoveBit,
                originalAlpha,
                originalBeta
            );
            return bestScore;
        }

        Score bestScore = INF;
        Bitboard bestMoveBit = 0;
        for (Bitboard moveBit : legalMoveBits) {
            MoveRecord moveRecord = board.applyMoveBit(moveBit, currentPlayer);
            Score score = perfectPlay(board, opponentPlayer, rootPlayer, alpha, beta);
            board.undoMove(moveRecord);

            if (timedOut) {
                break;
            }

            if (score < bestScore) {
                bestScore = score;
                bestMoveBit = moveBit;
            }
            if (score < beta) {
                beta = score;
            }
            if (beta <= alpha) {
                break;
            }
        }

        storePerfectCache(
            cacheKey,
            bestScore,
            bestMoveBit,
            originalAlpha,
            originalBeta
        );
        return bestScore;
    }

    // ゲーム終了時の石数差を大きな係数でスコア化します。
    Score finalScore(const Board& board, char targetPlayer) const {
        char opponent = opponentOf(targetPlayer);
        int myCount = board.countStones(targetPlayer);
        int opponentCount = board.countStones(opponent);
        return static_cast<Score>(myCount - opponentCount) * 100000;
    }
};

// CodinGame形式の入力を読み続け、各ターンの着手を出力します。
int main() {
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);

    int playerId = 0;
    int boardSize = 0;

    if (!(std::cin >> playerId)) {
        return 0;
    }
    std::cin >> boardSize;

    char myPlayer = static_cast<char>('0' + playerId);
    MiniMaxOthelloAi ai(playerId);

    while (true) {
        std::vector<std::string> boardLines;
        std::string boardLine;

        for (int row = 0; row < boardSize; ++row) {
            if (!(std::cin >> boardLine)) {
                return 0;
            }
            boardLines.push_back(boardLine);
        }

        int actionCount = 0;
        std::cin >> actionCount;
        std::vector<std::string> actions;

        for (int actionIndex = 0; actionIndex < actionCount; ++actionIndex) {
            std::string action;
            std::cin >> action;
            std::transform(action.begin(), action.end(), action.begin(), [](unsigned char character) {
                return static_cast<char>(std::tolower(character));
            });
            actions.push_back(action);
        }

        Board currentBoard = Board::fromLines(boardLines);

        std::string selectedAction = findPositionBookMove(currentBoard, myPlayer, actions);
        if (!selectedAction.empty()) {
            debug("standard=" + selectedAction);
        }

        if (selectedAction.empty()) {
            selectedAction = ai.chooseMove(currentBoard, actions);
        }

        if (selectedAction != "PASS"
            && std::find(actions.begin(), actions.end(), selectedAction) == actions.end()) {
            debug("invalid selected action guarded=" + selectedAction);

            if (!actions.empty()) {
                selectedAction = actions[0];
            } else {
                selectedAction = "PASS";
            }
        }

        std::cout << selectedAction << std::endl;
    }
}
