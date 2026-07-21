#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>
#include <unordered_map>
#include <unordered_set>
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
constexpr int PERFECT_EMPTY_LIMIT = 12;
constexpr std::size_t MAX_TRANSPOSITION_ENTRIES = 250000;
constexpr std::size_t MAX_PERFECT_CACHE_ENTRIES = 250000;

constexpr Bitboard FILE_A = 0x0101010101010101ULL;
constexpr Bitboard FILE_H = 0x8080808080808080ULL;
constexpr Bitboard NOT_A_FILE = ALL_MASK ^ FILE_A;
constexpr Bitboard NOT_H_FILE = ALL_MASK ^ FILE_H;

const std::array<int, 64> POSITION_WEIGHTS = {
    3000, -500, 10, 5, 5, 10, -500, 3000,
    -500, -800, -10, -10, -10, -10, -800, -500,
    10, -10, 20, 0, 0, 20, -10, 10,
    5, -10, 0, 0, 0, 0, -10, 5,
    5, -10, 0, 0, 0, 0, -10, 5,
    10, -10, 20, 0, 0, 20, -10, 10,
    -500, -800, -10, -10, -10, -10, -800, -500,
    3000, -500, 10, 5, 5, 10, -500, 3000
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

// 定石履歴をunordered_mapのキーに変換します。
std::string createHistoryKey(const std::vector<std::string>& historyActions) {
    std::string key;
    for (const std::string& action : historyActions) {
        key += action;
    }
    return key;
}

// 全定石へ8種類の対称変換を適用し、履歴から次手を引ける辞書を構築します。
std::unordered_map<std::string, std::vector<std::string>> buildStandardBook() {
    std::unordered_map<std::string, std::vector<std::string>> standardBook;

    for (const std::string& line : STANDARD_MOVE_LINES) {
        std::vector<std::string> originalMoves = splitStandardLine(line);

        for (int transformId = 0; transformId < 8; ++transformId) {
            std::vector<std::string> transformedMoves;

            for (const std::string& move : originalMoves) {
                std::pair<int, int> position = actionToPosition(move);
                std::pair<int, int> transformedPosition = transformPosition(
                    position.first,
                    position.second,
                    transformId
                );
                transformedMoves.push_back(positionToAction(
                    transformedPosition.first,
                    transformedPosition.second
                ));
            }

            std::vector<std::string> history;
            for (const std::string& nextMove : transformedMoves) {
                std::string key = createHistoryKey(history);
                std::vector<std::string>& candidates = standardBook[key];

                if (std::find(candidates.begin(), candidates.end(), nextMove) == candidates.end()) {
                    candidates.push_back(nextMove);
                }
                history.push_back(nextMove);
            }
        }
    }
    return standardBook;
}

const std::unordered_map<std::string, std::vector<std::string>> STANDARD_BOOK = buildStandardBook();

// 現在の履歴に対応し、かつ合法手に含まれる定石手を返します。
std::string findStandardMove(
    const std::vector<std::string>& historyActions,
    const std::vector<std::string>& legalActions
) {
    std::string key = createHistoryKey(historyActions);
    auto bookIterator = STANDARD_BOOK.find(key);

    if (bookIterator == STANDARD_BOOK.end()) {
        return "";
    }

    std::unordered_set<std::string> legalSet(legalActions.begin(), legalActions.end());
    for (const std::string& move : bookIterator->second) {
        if (legalSet.find(move) != legalSet.end()) {
            return move;
        }
    }
    return "";
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
        Bitboard moves = 0;

        for (ShiftFunction shiftFunction : SHIFT_FUNCTIONS) {
            Bitboard candidates = shiftFunction(playerBits) & opponentBits;

            for (int repeat = 0; repeat < 5; ++repeat) {
                candidates |= shiftFunction(candidates) & opponentBits;
            }
            moves |= shiftFunction(candidates) & emptyBits;
        }
        return moves;
    }

    // 合法手ビットボードを1手ずつのビット配列へ分解します。
    std::vector<Bitboard> getLegalMoveBitsList(char player) const {
        Bitboard movesBits = legalMovesBits(player);
        std::vector<Bitboard> legalMoveBits;

        while (movesBits != 0) {
            Bitboard moveBit = movesBits & (~movesBits + 1ULL);
            movesBits -= moveBit;
            legalMoveBits.push_back(moveBit);
        }
        return legalMoveBits;
    }

    // 1方向について、指定手で反転できる相手石を求めます。
    Bitboard getFlipsForDirection(
        Bitboard moveBit,
        ShiftFunction shiftFunction,
        Bitboard playerBits,
        Bitboard opponentBits
    ) const {
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
        Bitboard flips = 0;

        for (ShiftFunction shiftFunction : SHIFT_FUNCTIONS) {
            flips |= getFlipsForDirection(moveBit, shiftFunction, playerBits, opponentBits);
        }
        return flips;
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
};

// 直前のシミュレーション盤面と現在盤面との差分から、相手が置いた手を推定します。
std::vector<std::string> inferNewMoves(
    const Board& previousBoard,
    const Board& currentBoard,
    char placedPlayer
) {
    std::vector<std::string> newMoves;
    Bitboard previousOccupied = previousBoard.occupiedBoard();
    Bitboard currentPlayerBits = currentBoard.playerBoard(placedPlayer);
    Bitboard newBits = currentPlayerBits & ~previousOccupied;

    while (newBits != 0) {
        Bitboard moveBit = newBits & (~newBits + 1ULL);
        newBits -= moveBit;
        newMoves.push_back(bitToAction(moveBit));
    }
    return newMoves;
}

class Evaluator {
public:
    // 盤面の各マスに設定した位置価値の合計差を評価します。
    Score evaluateBoardPosition(const Board& board, char player) const {
        Bitboard playerBits = board.playerBoard(player);
        Bitboard opponentBits = board.opponentBoard(player);
        Score score = 0;

        while (playerBits != 0) {
            Bitboard moveBit = playerBits & (~playerBits + 1ULL);
            playerBits -= moveBit;
            score += POSITION_WEIGHTS[__builtin_ctzll(moveBit)];
        }

        while (opponentBits != 0) {
            Bitboard moveBit = opponentBits & (~opponentBits + 1ULL);
            opponentBits -= moveBit;
            score -= POSITION_WEIGHTS[__builtin_ctzll(moveBit)];
        }
        return score;
    }

    // 指定手の着手位置に設定した手順価値を返します。
    Score evaluateMoveScoreByBit(Bitboard moveBit) const {
        return MOVE_WEIGHTS[__builtin_ctzll(moveBit)];
    }

    // 自分と相手のパス回数差を評価します。
    Score evaluatePass(int myPassCount, int opponentPassCount) const {
        return static_cast<Score>(opponentPassCount - myPassCount) * 50;
    }

    // ゲーム進行度に応じて石数差を評価します。
    Score evaluateStoneCount(const Board& board, char player) const {
        char opponent = opponentOf(player);
        int myCount = board.countStones(player);
        int opponentCount = board.countStones(opponent);
        int emptyCount = board.countEmptyCells();

        if (myCount == 0) {
            return -2147483647LL;
        }
        if (emptyCount >= 40) {
            return static_cast<Score>(opponentCount - myCount) * 2;
        }
        if (emptyCount >= 15) {
            return 0;
        }
        return static_cast<Score>(myCount - opponentCount) * 5;
    }

    // 自分と相手の合法手数差を評価します。
    Score evaluateMobility(const Board& board, char player) const {
        char opponent = opponentOf(player);
        int myMoves = __builtin_popcountll(board.legalMovesBits(player));
        int opponentMoves = __builtin_popcountll(board.legalMovesBits(opponent));
        return static_cast<Score>(myMoves - opponentMoves) * 30;
    }

    // 相手石または自石に隣接する空きマス数から潜在的な着手可能性を評価します。
    Score evaluatePotentialMobility(const Board& board, char player) const {
        Bitboard playerBits = board.playerBoard(player);
        Bitboard opponentBits = board.opponentBoard(player);
        Bitboard emptyBits = ~board.occupiedBoard();

        int myPotential = __builtin_popcountll(neighborBits(opponentBits) & emptyBits);
        int opponentPotential = __builtin_popcountll(neighborBits(playerBits) & emptyBits);
        return static_cast<Score>(myPotential - opponentPotential) * 3;
    }

    // 空きマスに接している不安定な石の数を評価します。
    Score evaluateFrontier(const Board& board, char player) const {
        Bitboard playerBits = board.playerBoard(player);
        Bitboard opponentBits = board.opponentBoard(player);
        Bitboard emptyNeighbors = neighborBits(~board.occupiedBoard());

        int myFrontier = __builtin_popcountll(playerBits & emptyNeighbors);
        int opponentFrontier = __builtin_popcountll(opponentBits & emptyNeighbors);
        return static_cast<Score>(opponentFrontier - myFrontier) * 5;
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

    // 角と角から続く辺の確定石を近似評価します。
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
        return static_cast<Score>(myStable - opponentStable) * 30;
    }

    // 各評価項目を合算して、指定プレイヤー視点の盤面評価値を返します。
    Score evaluateAll(
        const Board& board,
        char player,
        int myPassCount,
        int opponentPassCount
    ) const {
        Score score = 0;
        score += evaluateBoardPosition(board, player);
        score += evaluateStoneCount(board, player);
        score += evaluateMobility(board, player);
        score += evaluatePotentialMobility(board, player);
        score += evaluateFrontier(board, player);
        score += evaluateStability(board, player);
        score += evaluatePass(myPassCount, opponentPassCount);
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
    std::unordered_map<PerfectCacheKey, PerfectCacheEntry, PerfectCacheKeyHash> perfectCache;
    std::unordered_map<TranspositionKey, TranspositionEntry, TranspositionKeyHash> transpositionTable;

public:
    // AIが担当するプレイヤーを設定して探索器を初期化します。
    explicit MiniMaxOthelloAi(int playerId)
        : player(static_cast<char>('0' + playerId)), timedOut(false) {
        transpositionTable.reserve(131072);
        perfectCache.reserve(131072);
    }

    // 現在時刻が探索終了時刻へ到達したか判定します。
    bool isTimeUp() const {
        return Clock::now() >= endTime;
    }

    // 保存済み最善手を先頭にし、残りを位置評価の高い順へ並べ替えます。
    void sortMoves(std::vector<Bitboard>& moveBits, Bitboard preferredMoveBit = 0) const {
        std::sort(
            moveBits.begin(),
            moveBits.end(),
            [this](Bitboard leftMove, Bitboard rightMove) {
                return evaluator.evaluateMoveScoreByBit(leftMove)
                    > evaluator.evaluateMoveScoreByBit(rightMove);
            }
        );

        auto preferredIterator = std::find(
            moveBits.begin(),
            moveBits.end(),
            preferredMoveBit
        );
        if (preferredIterator != moveBits.end()) {
            std::rotate(moveBits.begin(), preferredIterator, preferredIterator + 1);
        }
    }

    // 合法手から、定められた制限時間内で最善手を選択します。
    std::string chooseMove(Board& board, const std::vector<std::string>& legalActions) {
        if (legalActions.empty()) {
            return "PASS";
        }

        std::vector<Bitboard> legalMoveBits;
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
        timedOut = false;

        // 前の手番で作成した有効なキャッシュは再利用し、肥大化した場合だけ破棄します。
        if (transpositionTable.size() >= MAX_TRANSPOSITION_ENTRIES) {
            transpositionTable.clear();
        }
        if (perfectCache.size() >= MAX_PERFECT_CACHE_ENTRIES) {
            perfectCache.clear();
        }

        sortMoves(legalMoveBits);

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
        const int maxDepth = 64;
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

                // 現在深度を読み切れなくても、完了済みルート手の最善手は保持します。
                if (result.hasBestMove) {
                    bestMoveBit = result.bestMoveBit;
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
        sortMoves(orderedMoveBits, preferredMoveBit);

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
            transpositionTable[cacheKey] = TranspositionEntry{
                depth,
                score,
                bestMoveBit,
                boundType
            };
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
            transpositionTable[cacheKey] = TranspositionEntry{
                depth,
                score,
                0,
                BoundType::EXACT
            };
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
            perfectCache[cacheKey] = PerfectCacheEntry{score, bestMoveBit, boundType};
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
        if (isTimeUp()) {
            timedOut = true;
            return evaluator.evaluateAll(
                board,
                rootPlayer,
                myPassCount,
                opponentPassCount
            );
        }

        int emptyCount = board.countEmptyCells();
        if (emptyCount <= PERFECT_EMPTY_LIMIT) {
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

        sortMoves(legalMoveBits, preferredMoveBit);

        if (currentPlayer == rootPlayer) {
            Score bestScore = -INF;
            Bitboard bestMoveBit = 0;
            bool isFirstMove = true;

            for (Bitboard moveBit : legalMoveBits) {
                if (isTimeUp()) {
                    timedOut = true;
                    break;
                }

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
            if (isTimeUp()) {
                timedOut = true;
                break;
            }

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

    // 空きマスが少ない終盤をゲーム終了まで完全探索します。
    Score perfectPlay(
        Board& board,
        char currentPlayer,
        char rootPlayer,
        Score alpha,
        Score beta
    ) {
        if (isTimeUp()) {
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
        sortMoves(legalMoveBits, preferredMoveBit);

        if (currentPlayer == rootPlayer) {
            Score bestScore = -INF;
            Bitboard bestMoveBit = 0;

            for (Bitboard moveBit : legalMoveBits) {
                if (isTimeUp()) {
                    timedOut = true;
                    break;
                }

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
            if (isTimeUp()) {
                timedOut = true;
                break;
            }

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
    char opponentPlayer = opponentOf(myPlayer);
    MiniMaxOthelloAi ai(playerId);

    std::vector<std::string> historyActions;
    Board lastSimulatedBoard(0, 0);
    bool hasLastSimulatedBoard = false;
    bool isFirstTurn = true;

    Board initialBoard = Board::fromLines({
        "........",
        "........",
        "........",
        "...01...",
        "...10...",
        "........",
        "........",
        "........"
    });

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

        if (hasLastSimulatedBoard) {
            std::vector<std::string> opponentMoves = inferNewMoves(
                lastSimulatedBoard,
                currentBoard,
                opponentPlayer
            );
            historyActions.insert(historyActions.end(), opponentMoves.begin(), opponentMoves.end());
        }

        if (isFirstTurn) {
            if (myPlayer == PLAYER_ONE) {
                std::vector<std::string> firstOpponentMoves = inferNewMoves(
                    initialBoard,
                    currentBoard,
                    opponentPlayer
                );

                for (const std::string& opponentMove : firstOpponentMoves) {
                    if (std::find(historyActions.begin(), historyActions.end(), opponentMove) == historyActions.end()) {
                        historyActions.push_back(opponentMove);
                    }
                }
            }
            isFirstTurn = false;
        }

        std::string selectedAction = findStandardMove(historyActions, actions);
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

        if (selectedAction != "PASS") {
            historyActions.push_back(selectedAction);
            Bitboard selectedBit = actionToBit(selectedAction);
            Board simulatedBoard = currentBoard.copy();
            simulatedBoard.applyMoveBit(selectedBit, myPlayer);
            lastSimulatedBoard = simulatedBoard;
            hasLastSimulatedBoard = true;
        } else {
            lastSimulatedBoard = currentBoard.copy();
            hasLastSimulatedBoard = true;
        }

        std::cout << selectedAction << std::endl;
    }
}
