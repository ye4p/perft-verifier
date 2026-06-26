#include "board.h"
#include <array>
#include <vector>
#include <string>
#include <sstream>
#include <cctype>
#include <cstdint>
#include <iostream>
#include <algorithm>
#include <unordered_map>
#include <cstdio>
#include <random>
#include <chrono>
#include <thread>

Undo::Undo()
{
    castlingRights = 0b1111;
    capturedPiece = -1;
    enPassantSquare = -1;
    halfMoveClock = 0;
    fullMoveClock = 1;
}

Undo::Undo(uint8_t castlingRights, int capturedPiece, int enPassantSquare, uint8_t halfMoveClock, uint16_t fullMoveClock)
{
    this->castlingRights = castlingRights;
    this->capturedPiece = capturedPiece;
    this->enPassantSquare = enPassantSquare;
    this->halfMoveClock = halfMoveClock;
    this->fullMoveClock = fullMoveClock;
}

std::ostream &operator<<(std::ostream &os, const Undo &u)
{
    os << "Undo castling rights:" << u.castlingRights << "captured piece " << u.capturedPiece << " en passant square: " << u.enPassantSquare << ".";
    return os;
}

//

Move::Move()
{
    data = 0;
}
Move::Move(int from, int to, int status = 0)
{
    data = ((status << 12) | (to << 6) | from);
}

int Move::from() const
{
    return (data & 0x3F);
}
int Move::to() const
{
    return ((data >> 6) & 0x3F);
}
int Move::status() const
{
    return (((data >> 12) & 0xF));
}

bool Move::isCastling() const
{
    return (status() == 0x2 || status() == 0x3);
}
bool Move::isEnPassant() const
{
    return (status() == 0x5); // 0b0101
}
bool Move::isPromotion() const
{
    return (status() & 0x8); // 0b1000
}
bool Move::isCapture() const
{
    return (status() & 0x4); // 0b0100
}

std::ostream &operator<<(std::ostream &os, const Move &m)
{
    os << "Move from " << m.from() << " to " << m.to() << " with status: " << m.status(); //<< "\n";
    return os;
}

//

uint64_t Board::pawn_masks[2][64];
uint64_t Board::pawn_push[2][64];
uint64_t Board::pawn_double_push[2][64];
uint64_t Board::knight_masks[64];
uint64_t Board::king_masks[64];
uint64_t Board::bishop_masks[64];
uint64_t Board::rook_masks[64];

uint64_t Board::bishop_relevant_bits[64];
uint64_t Board::rook_relevant_bits[64];

uint64_t Board::bishop_attacks[64][512];
uint64_t Board::rook_attacks[64][4096];

//  Constructor
Board::Board()
{
    bbs.fill(0ULL);
    occupancies.fill(0ULL);
    mailbox.fill(-1);
    castlingRights = 0b0000;
    enPassantSquare = -1;
    halfMoveClock = 0;
    fullMoveClock = 1;
    sideToMove = 0;
    // std::cout << " Generating King moves\n";
    generateKingMoves();
    // std::cout << " Generating pawn moves\n";
    generatePawnMoves();
    // std::cout << " Generating knight moves\n";
    generateKnightMoves();
    // std::cout << "Searching for magic numbers\n";
    // searchAllMagics();
    // std::cout << " Generating bishop moves\n";
    generateBishopMoves();
    // std::cout << " Generating rook moves\n";
    generateRookMoves();
    // init_zobrist();
    // std::cout<< " Fin\n";
}

void Board::clean()
{
    bbs.fill(0ULL);
    occupancies.fill(0ULL);
    mailbox.fill(-1);
    castlingRights = 0;
    enPassantSquare = -1;
    halfMoveClock = 0;
    fullMoveClock = 1;
    sideToMove = 0;
    moveList.fill(Move());
    undoList.fill(Undo());
    moveScores.fill(0);
    ply = 0;
    timeUp = false;
    nodes = 0;
    timeLimitMs = 0;
}

//
//      HELPER METHODS FOR BITWISE INTERACTION:
//
void Board::setBit(uint64_t &bb, int square)
{
    bb |= (1ULL << square);
}

void Board::clearBit(uint64_t &bb, int square)
{
    bb &= ~(1ULL << square);
}

bool Board::isBitSet(uint64_t bb, int square)
{
    return (bb & (1ULL << square)) > 0;
}
int Board::pieceOn(int square)
{
    for (int i = 0; i < 12; i++)
    {
        if (isBitSet(bbs[i], square))
        {
            return i;
        }
    }
    return -1;
}
uint64_t Board::get_lsb_bb(uint64_t bb)
{
    if (!bb)
    {
        throw std::runtime_error("BB is already empty, can't GET lsb");
    }

    return (bb & ((~bb) + 1));
}
uint64_t Board::pop_lsb_bb(uint64_t &bb)
{
    if (!bb)
    {
        throw std::runtime_error("BB is already empty, can't POP lsb");
    }
    int square = __builtin_ctzll(bb);
    bb &= bb - 1;
    return square;
}
int Board::get_lsb_index(uint64_t bb)
{
    return __builtin_ctzll(bb);
}

inline int Board::popcount(uint64_t bb)
{
    // int count=0;
    // while (bb) {
    //     bb&=bb-1;
    //     count++;
    // }
    return __builtin_popcountll(bb);
}

void Board::updateOccupancies()
{
    occupancies[0] = 0ULL;
    occupancies[1] = 0ULL;
    occupancies[2] = 0ULL;
    for (int i = 0; i <= 5; i++)
    {
        occupancies[0] |= bbs[i];
    }
    for (int i = 6; i <= 11; i++)
    {
        occupancies[1] |= bbs[i];
    }
    occupancies[2] = occupancies[0] | occupancies[1];
}

#ifdef DEBUG
int Board::findPiece(int square, Move m)
{
    for (int i = 0; i < 12; i++)
    {
        if (bbs[i] & (1ULL << square))
        {
            return i;
        }
    }
    std::cout << "Move sequence that led to this error position: ";
    for (Move m : moveLog)
    {
        std::cout << moveToCode(m) << ", ";
    }
    std::cout << "\n";
    for (auto i = 0; i < moveLog.size(); i++)
    {
        std::cout << moveToCode(moveLog[i]) << ", and corresponding board:\n";
        displayBB(boardLog[i]);
    }
    std::cout << "\n";
    displayBoard();
    std::cout << "ERROR with square " << square << " with move: " << m << "\n";
    throw std::runtime_error("Piece not found");
}
#endif

int Board::findPieceKing(int square, Move m)
{
    for (int i = 0; i < 12; i++)
    {
        if (bbs[i] & (1ULL << square))
        {
            return i;
        }
    }
    return -1;
}

void Board::validateBoard(int i)
{
    std::string s = "";
    if (i == 1)
    {
        s += "perft after making move, ";
    }
    else if (i == 2)
    {
        s += "perft after undoing move, ";
    }
    else if (i == 3)
    {
        s += "perft divide after making move, ";
    }
    else if (i == 4)
    {
        s += "perft divide after undoing move, ";
    }
    if (!bbs[5])
    {
        throw std::runtime_error(s + "no white king found");
    }
    if (!bbs[11])
    {
        throw std::runtime_error(s + "no black king found");
    }
}

uint64_t Board::getBB()
{
    uint64_t bb = 0ULL;
    for (int i = 0; i < 12; i++)
    {
        bb |= bbs[i];
    }
    return bb;
}

//
//  DISPLAYING STUFF
//

void Board::displayBoard()
{
    std::cout << "\n";
    for (int rank = 7; rank > -1; rank--)
    {
        for (int file = 0; file < 8; file++)
        {
            int square = rank * 8 + file;
            if (!file)
            {
                printf(" %d ", rank + 1);
            }
            printf(" %d", (isBitSet(occupancies[2], square) ? 1 : 0));
        }
        std::cout << "\n";
    }
    printf("\n    a b c d e f g h\n\n");
    printf("bitboard: %lld\n\n", occupancies[2]);
}

void Board::displayBB(uint64_t bb)
{
    std::cout << "\n";
    for (int rank = 7; rank > -1; rank--)
    {
        for (int file = 0; file < 8; file++)
        {
            int square = rank * 8 + file;
            if (!file)
            {
                printf(" %d ", rank + 1);
            }
            printf(" %d", (isBitSet(bb, square) ? 1 : 0));
        }
        std::cout << "\n";
    }
    printf("\n    a b c d e f g h\n\n");
    printf("bitboard: %lld\n\n", bb);
}

void Board::displayMailbox()
{
    std::cout << "\n";
    for (int rank = 7; rank > -1; rank--)
    {
        for (int file = 0; file < 8; file++)
        {
            int square = rank * 8 + file;
            if (!file)
            {
                printf(" %d ", rank + 1);
            }
            printf(" %d", mailbox[square]);
        }
        std::cout << "\n";
    }
    printf("\n    a b c d e f g h\n\n");
    printf("bitboard: %lld\n\n", occupancies[2]);
}

void Board::displayMailbox(std::array<int, 64> &mb)
{
    std::cout << "\n";
    for (int rank = 7; rank > -1; rank--)
    {
        for (int file = 0; file < 8; file++)
        {
            int square = rank * 8 + file;
            if (!file)
            {
                printf(" %d ", rank + 1);
            }
            printf(" %d", mb[square]);
        }
        std::cout << "\n";
    }
    printf("\n    a b c d e f g h\n\n");
    printf("bitboard: %lld\n\n", occupancies[2]);
}

void Board::displayMoves(std::array<Move, 256> moveList)
{
    for (int i = 0; i < moveList.size(); i++)
    {
        std::cout << moveList[i];
    }
}

//
// FEN PROCESSING
//

std::vector<std::string> Board::splitString(std::string str, char delimiter)
{
    std::vector<std::string> tokens;
    std::string token;
    std::stringstream ss(str);
    while (std::getline(ss, token, delimiter))
    {
        tokens.push_back(token);
    }
    return tokens;
}

int Board::codeToIndex(std::string code)
{
    char file = code[0] - 'a';
    char rank = code[1] - '1';
    return rank * 8 + file;
}

std::string Board::indexToCode(int index)
{
    int file = index % 8;
    int rank = index / 8;
    std::string s;
    s += char('a' + file);
    s += char('1' + rank);
    return s;
}

std::string Board::moveToCode(Move m)
{
    int r1 = m.from() / 8;
    int f1 = m.from() % 8;
    int r2 = m.to() / 8;
    int f2 = m.to() % 8;
    std::string s = "";
    // s += char('a' + f1) + char('1' + r1);
    // s += char('a' + f2) + char('1' + r2);
    auto l1 = fileMap.find(f1);
    auto l2 = fileMap.find(f2);
    s += char(l1->second);
    s += char('1' + r1);
    s += char(l2->second);
    s += char('1' + r2);
    if (m.isPromotion())
    {
        int stat = m.status();
        if (stat == 0b1100 || stat == 0b1000)
        {
            s += 'n';
        }
        else if (stat == 0b1001 || stat == 0b1101)
        {
            s += 'b';
        }
        else if (stat == 0b1010 || stat == 0b1110)
        {
            s += 'r';
        }
        else
        {
            s += 'q';
        }
    }
    return s;
}

Move Board::codeToMove(std::string s)
{
    int s1 = s[0] - 'a';
    int s2 = s[1] - '0';
    int e1 = s[2] - 'a';
    int e2 = s[3] - '0';
    int squareStart = (s2 - 1) * 8 + s1;
    int squareEnd = (e2 - 1) * 8 + e1;
    int status = 0b0000;
    if ((mailbox[squareEnd] > 5) && !sideToMove)
    {
        status |= 0b0100;
    }
    else if ((mailbox[squareEnd] > -1) && (mailbox[squareEnd] < 6) && sideToMove)
    {
        status |= 0b0100;
    }
    if ((mailbox[squareStart] % 6) == 5 && std::abs(squareEnd - squareStart) == 2)
    {
        status = (squareEnd > squareStart) ? 0b0010 : 0b0011;
    }
    if (squareEnd == enPassantSquare && ((mailbox[squareStart] % 6) == 0))
    {
        status = 0b0101;
    }
    else if (((mailbox[squareStart] % 6) == 0) && (std::abs(squareEnd - squareStart) == 16))
    {
        status = 0b0001;
    }
    else if (s.length() > 4)
    {
        char promotion = s[4];
        if (mailbox[squareEnd] != -1)
        {
            status |= 0b0100;
        }
        if (promotion == 'q')
        {
            status |= 0b1011;
        }
        else if (promotion == 'r')
        {
            status |= 0b1010;
        }
        else if (promotion == 'b')
        {
            status |= 0b1001;
        }
        else
        {
            status |= 0b1000;
        }
    }
    return Move(squareStart, squareEnd, status);
}

std::string Board::getFEN()
{
    return " ";
}

void Board::setFEN(std::string s)
{
    std::vector<std::string> vec = splitString(s, ' ');
    std::vector<std::string> rows = splitString(vec[0], '/');

    // Clear prev position:
    bbs.fill(0ULL);
    castlingRights = 0b0000;
    enPassantSquare = -1;
    halfMoveClock = 0;
    fullMoveClock = 0;
    sideToMove = 0;
    // whiteToMove = true;

    int rank = 7;
    int file = 0;
    for (std::string row : rows)
    {
        for (char let : row)
        {
            if (std::isalpha(let))
            {
                int color = (std::isupper(let) ? 0 : 6);
                char letConverted = std::tolower(let);
                int piece = pieceMap.at(letConverted);
                setBit(bbs[piece + color], rank * 8 + file);
                mailbox[rank * 8 + file] = piece + color;
                file++;
            }
            if (std::isdigit(let))
            {
                int letConverted = let - '0';
                for (int i = 0; i < letConverted; i++)
                {
                    mailbox[rank * 8 + file + i] = -1;
                }
                file += letConverted;
            }
        }
        rank--;
        file = 0;
    }
    if (vec[1] == "w")
    {
        sideToMove = 0;
    }
    else
    {
        sideToMove = 1;
    }

    for (char let : vec[2])
    {
        switch (let)
        {
        case 'K':
        {
            castlingRights |= (1 << 3);
            break;
        }
        case 'Q':
        {
            castlingRights |= (1 << 2);
            break;
        }
        case 'k':
        {
            castlingRights |= (1 << 1);
            break;
        }
        case 'q':
        {
            castlingRights |= (1 << 0);
            break;
        }
        }
    }
    int squareEnPassant = codeToIndex(vec[3]);
    if (vec[3] == "-")
    {
        enPassantSquare = -1;
    }
    else
    {
        enPassantSquare = squareEnPassant;
    }
    if (vec.size() > 4)
    {
        if (vec[4] == "-" || vec[5] == "-")
        {
            halfMoveClock = 0;
            fullMoveClock = 0;
        }
        else
        {
            halfMoveClock = std::stoi(vec[4]);
            fullMoveClock = std::stoi(vec[5]);
        }
    }
    updateOccupancies();
}

//
// INITIALIZATION/PREGENERATION STAGE
//

// Magic bbs gen
uint64_t Board::rand64()
{
    static std::mt19937_64 rng(std::random_device{}());
    return rng();
}

uint64_t Board::sparse_rand()
{
    return rand64() & rand64() & rand64();
}

uint64_t Board::find_magic(int sq, bool bishop)
{
    uint64_t mask = bishop ? mask_bishop_attacks(sq) : mask_rook_attacks(sq);
    int bits = popcount(mask);
    int size = 1 << bits;

    //  Build occupancy/attack pairs
    uint64_t occs[4096], atks[4096];
    for (int i = 0; i < size; i++)
    {
        occs[i] = set_occupancy(i, bits, mask);
        atks[i] = bishop ? bishop_attacks_from_occupancy(sq, occs[i]) : rook_attacks_from_occupancy(sq, occs[i]);
    }

    //  Trial and error:
    for (int attempt = 0; attempt < 100000000; attempt++)
    {
        uint64_t magic = sparse_rand();
        // if (popcount((mask * magic) >> 56) < 6)
        //     continue;
        uint64_t used[4096] = {};
        bool fail = false;
        for (int i = 0; i < size && !fail; i++)
        {
            int idx = (occs[i] * magic) >> (64 - bits);
            if (!used[idx])
            {
                used[idx] = atks[i];
            }
            else if (used[idx] != atks[i])
            {
                fail = true;
            }
        }
        if (!fail)
        {
            return magic;
        }
    }
    return 0ULL;
}

// void Board::searchAllMagics()
// {
//     for (int i = 0; i < 64; i++)
//     {
//         BISHOP_MAGICS[i] = find_magic(i, true);
//         ROOK_MAGICS[i] = find_magic(i, false);
//         if (!BISHOP_MAGICS[i])
//             throw std::runtime_error("BISHOP MAGIC FAILED");
//         if (!ROOK_MAGICS[i])
//             throw std::runtime_error("ROOK MAGIC FAILED");
//     }

//     // for (int i : BISHOP_MAGICS) {
//     //     std::cout<<i<<", ";
//     // }
//     // std::cout<< "\n\n\n\n";
//     // for (int i : ROOK_MAGICS) {
//     //     std::cout<<i<<", ";
//     // }
// }

uint64_t Board::mask_pawn_attacks(int side, int square)
{
    uint64_t attack = 0ULL;

    int rank = square / 8;
    int file = square % 8;

    int f1 = file + 1;
    int f2 = file - 1;
    int r = rank + (!side ? +1 : -1);

    if (r >= 0 && r < 8 && f1 >= 0 && f1 < 8)
    {
        attack |= (1ULL << (r * 8 + f1));
    }
    if (r >= 0 && r < 8 && f2 >= 0 && f2 < 8)
    {
        attack |= (1ULL << (r * 8 + f2));
    }
    // if (attack > 0)
    // {
    //     displayBB(attack);
    // }
    return attack;
}

uint64_t Board::mask_pawn_push(int side, int square, int push)
{
    uint64_t attack = 0ULL;
    int rank = square / 8;
    int file = square % 8;
    int dir = (side ? -1 : 1);
    int r = rank + (push * dir);
    if (r >= 0 && r < 8)
    {
        attack |= (1ULL << (r * 8 + file));
    }
    // if (attack)
    // {
    //     displayBB(attack);
    // }

    return attack;
}

uint64_t Board::mask_knight_attacks(int square)
{
    uint64_t attacks = 0ULL;

    int rank = square / 8;
    int file = square % 8;

    int dr[] = {-2, -1, 1, 2, 2, 1, -1, -2};
    int df[] = {1, 2, 2, 1, -1, -2, -2, -1};

    for (int i = 0; i < 8; i++)
    {
        int r = rank + dr[i];
        int f = file + df[i];

        if (r >= 0 && r < 8 && f >= 0 && f < 8)
        {
            attacks |= (1ULL << (r * 8 + f));
        }
    }
    // if (attacks > 0)
    // {
    //     displayBoard(attacks);
    // }
    return attacks;
}
uint64_t Board::mask_king_attacks(int square)
{
    uint64_t attacks = 0ULL;

    int rank = square / 8;
    int file = square % 8;

    int dr[] = {-1, 0, 1, -1, 1, -1, 0, 1};
    int df[] = {-1, -1, -1, 0, 0, 1, 1, 1};

    for (int i = 0; i < 8; i++)
    {
        int r = rank + dr[i];
        int f = file + df[i];
        if (r >= 0 && r < 8 && f >= 0 && f < 8)
        {
            attacks |= (1ULL << (r * 8 + f));
        }
    }
    // if (attacks > 0)
    // {
    //     displayBoard(attacks);
    // }
    return attacks;
}
uint64_t Board::mask_bishop_attacks(int square)
{
    uint64_t attacks = 0ULL;

    int rank = square / 8;
    int file = square % 8;

    int dr[] = {-1, -1, 1, 1};
    int df[] = {-1, 1, -1, 1};

    for (int i = 0; i < 4; i++)
    {
        int r = rank + dr[i];
        int f = file + df[i];
        while (r > 0 && r < 7 && f > 0 && f < 7)
        {
            attacks |= (1ULL << (r * 8 + f));
            r += dr[i];
            f += df[i];
        }
    }
    // if (attacks>0) {
    //     displayBoard(attacks);
    // }
    return attacks;
}

uint64_t Board::mask_rook_attacks(int square)
{
    uint64_t attacks = 0ULL;

    int rank = square / 8;
    int file = square % 8;

    int dr[] = {0, 0, -1, 1};
    int df[] = {-1, 1, 0, 0};

    for (int i = 0; i < 4; i++)
    {
        int r = rank + dr[i];
        int f = file + df[i];
        while (((r > 0 && r < 7) || ((rank == 0 || rank == 7) && df[i] != 0)) && ((f > 0 && f < 7) || ((file == 0 || file == 7) && dr[i] != 0)))
        {
            attacks |= (1ULL << (r * 8 + f));
            r += dr[i];
            f += df[i];
        }
    }
    // if (attacks>0) {
    //     displayBoard(attacks);
    // }
    return attacks;
}

uint64_t Board::set_occupancy(int index, int bits, uint64_t mask)
{
    uint64_t occupancy = 0ULL;
    for (int i = 0; i < bits; i++)
    {
        int square = get_lsb_index(mask);
        mask &= mask - 1;
        if (index & (1 << i))
        {
            occupancy |= (1ULL << square);
        }
    }
    return occupancy;
}

uint64_t Board::bishop_attacks_from_occupancy(int square, uint64_t blockers)
{
    uint64_t attacks = 0ULL;
    int r = square / 8;
    int f = square % 8;

    // NE
    for (int i = r + 1, j = f + 1; i <= 7 && j <= 7; i++, j++)
    {
        attacks |= (1ULL << (i * 8 + j));
        if (blockers & (1ULL << (i * 8 + j)))
            break;
    }
    // NW
    for (int i = r + 1, j = f - 1; i <= 7 && j >= 0; i++, j--)
    {
        attacks |= (1ULL << (i * 8 + j));
        if (blockers & (1ULL << (i * 8 + j)))
            break;
    }
    // SE
    for (int i = r - 1, j = f + 1; i >= 0 && j <= 7; i--, j++)
    {
        attacks |= (1ULL << (i * 8 + j));
        if (blockers & (1ULL << (i * 8 + j)))
            break;
    }
    // SW
    for (int i = r - 1, j = f - 1; i >= 0 && j >= 0; i--, j--)
    {
        attacks |= (1ULL << (i * 8 + j));
        if (blockers & (1ULL << (i * 8 + j)))
            break;
    }
    return attacks;
}

uint64_t Board::rook_attacks_from_occupancy(int square, uint64_t blockers)
{
    uint64_t attacks = 0ULL;
    int r = square / 8;
    int f = square % 8;
    // N
    for (int i = r + 1; i <= 7; i++)
    {
        attacks |= (1ULL << (i * 8 + f));
        if (blockers & (1ULL << (i * 8 + f)))
            break;
    }
    // S
    for (int i = r - 1; i >= 0; i--)
    {
        attacks |= (1ULL << (i * 8 + f));
        if (blockers & (1ULL << (i * 8 + f)))
            break;
    }
    // E
    for (int j = f + 1; j <= 7; j++)
    {
        attacks |= (1ULL << (r * 8 + j));
        if (blockers & (1ULL << (r * 8 + j)))
            break;
    }
    // W
    for (int j = f - 1; j >= 0; j--)
    {
        attacks |= (1ULL << (r * 8 + j));
        if (blockers & (1ULL << (r * 8 + j)))
            break;
    }
    return attacks;
}

//
// MAIN MOVE GENERATION
//

int Board::generateMoves(const int offset)
{
    int count = offset;
    uint64_t from_bb;

    if (!sideToMove)
    {
        //
        //  WHITE PIECES MOVE GENERATION
        //

        // White pawns
        from_bb = bbs[0];
        while (from_bb > 0)
        {
            int from = pop_lsb_bb(from_bb);
            uint64_t pseudolegal = pawn_masks[0][from] & (occupancies[1] | ((enPassantSquare != -1) ? (1ULL << enPassantSquare) : 0ULL));
            while (pseudolegal > 0)
            {
                int to = pop_lsb_bb(pseudolegal);

                //  En passant
                // std::cout << "to: " << to << " enPassantSquare: " << enPassantSquare << "\n";
                if (to == enPassantSquare)
                {
                    // std::cout << "ADDING EN PASSANT\n";
                    moveList[count] = Move(from, to, 0b0101); // en passant status bitwise
                }
                else if ((0xff00000000000000 & (1ULL << to)) && ((1ULL << to) & occupancies[1]))
                { // If last row for white pawns(promotion)
                    moveList[count] = Move(from, to, 0b1100);
                    count++;
                    moveList[count] = Move(from, to, 0b1101);
                    count++;
                    moveList[count] = Move(from, to, 0b1110);
                    count++;
                    moveList[count] = Move(from, to, 0b1111);
                }
                else
                {
                    moveList[count] = Move(from, to, 0b0100);
                }
                // std::cout << "Added move " << Move(from, to, 0b0000);

                count++;
            }
            pseudolegal = pawn_push[0][from] & (~occupancies[2]);
            while (pseudolegal)
            {
                int to = pop_lsb_bb(pseudolegal);
                //  Regular Push
                if (0xff00000000000000 & (1ULL << to))
                {
                    moveList[count] = Move(from, to, 0b1000);
                    count++;
                    moveList[count] = Move(from, to, 0b1001);
                    count++;
                    moveList[count] = Move(from, to, 0b1010);
                    count++;
                    moveList[count] = Move(from, to, 0b1011);
                }
                else
                {
                    moveList[count] = Move(from, to, 0b0000);
                }
                count++;
            }
            if ((1ULL << from) & 0xff00)
            {
                pseudolegal = pawn_double_push[0][from] & (~occupancies[2]);
                while (pseudolegal)
                {
                    int to = pop_lsb_bb(pseudolegal);
                    //  Double Push
                    if (((pawn_double_push[0][from] | pawn_push[0][from]) & occupancies[2]) == 0)
                    {

                        moveList[count] = Move(from, to, 0b0001);
                        count++;
                    }
                }
            }
        }

        //  White knights
        from_bb = bbs[1];
        while (from_bb > 0)
        {
            int from = pop_lsb_bb(from_bb);
            uint64_t pseudolegal = knight_masks[from] & (~occupancies[0]);
            while (pseudolegal > 0)
            {
                int to = pop_lsb_bb(pseudolegal);
                if ((1ULL << to) & occupancies[1])
                {
                    moveList[count] = Move(from, to, 0b0100);
                }
                else
                {
                    moveList[count] = Move(from, to, 0b0000);
                }
                count++;
            }
        }

        // White bishops
        from_bb = bbs[2];
        while (from_bb > 0)
        {
            int from = pop_lsb_bb(from_bb);
            uint64_t pseudolegal = get_bishop_attacks(from, occupancies[2]) & ~occupancies[0];
            while (pseudolegal > 0)
            {
                int to = pop_lsb_bb(pseudolegal);
                if ((1ULL << to) & occupancies[1])
                {
                    moveList[count] = Move(from, to, 0b0100);
                }
                else
                {
                    moveList[count] = Move(from, to, 0b0000);
                }
                count++;
            }
        }

        // White rooks
        from_bb = bbs[3];
        while (from_bb > 0)
        {
            int from = pop_lsb_bb(from_bb);
            uint64_t pseudolegal = get_rook_attacks(from, occupancies[2]) & ~occupancies[0];
            while (pseudolegal > 0)
            {
                int to = pop_lsb_bb(pseudolegal);
                if ((1ULL << to) & occupancies[1])
                {
                    moveList[count] = Move(from, to, 0b0100);
                }
                else
                {
                    moveList[count] = Move(from, to, 0b0000);
                }
                count++;
            }
        }

        // White queens
        from_bb = bbs[4];
        while (from_bb > 0)
        {
            int from = pop_lsb_bb(from_bb);
            uint64_t pseudolegal = (get_bishop_attacks(from, occupancies[2]) | get_rook_attacks(from, occupancies[2])) & ~occupancies[0];
            while (pseudolegal > 0)
            {
                int to = pop_lsb_bb(pseudolegal);
                if ((1ULL << to) & occupancies[1])
                {
                    moveList[count] = Move(from, to, 0b0100);
                }
                else
                {
                    moveList[count] = Move(from, to, 0b0000);
                }
                count++;
            }
        }

        // White kings
        from_bb = bbs[5];
        while (from_bb > 0)
        {
            int from = pop_lsb_bb(from_bb);
            uint64_t pseudolegal = king_masks[from] & ~occupancies[0];
            while (pseudolegal > 0)
            {
                int to = pop_lsb_bb(pseudolegal);
                if ((1ULL << to) & occupancies[1])
                {
                    moveList[count] = Move(from, to, 0b0100);
                }
                else
                {
                    moveList[count] = Move(from, to, 0b0000);
                }
                count++;
            }
            // Queen side castling
            if (!isSquareAttacked(from - 1, 1) && (castlingRights & 0b0100) && !isSquareAttacked(from, 1))
            {
                if (((7ULL << 1) & (occupancies[2])) == 0)
                {
                    moveList[count] = Move(from, from - 2, 0b0011);
                    count++;
                }
            }
            // King side castling
            if (!isSquareAttacked(from + 1, 1) && (castlingRights & 0b1000) && !isSquareAttacked(from, 1))
            {
                if (((3ULL << 5) & (occupancies[2])) == 0)
                {
                    moveList[count] = Move(from, from + 2, 0b0010);
                    count++;
                }
            }
        }
    }
    else
    {

        //
        //  BLACK PIECES MOVE GENERATION
        //

        // Black pawns

        from_bb = bbs[6];
        while (from_bb > 0)
        {

            int from = pop_lsb_bb(from_bb);
            uint64_t pseudolegal = pawn_masks[1][from] & (occupancies[0] | ((enPassantSquare != -1) ? (1ULL << enPassantSquare) : 0ULL));
            while (pseudolegal > 0)
            {
                int to = pop_lsb_bb(pseudolegal);
                // std::cout << "to: " << to << " enPassantSquare: " << enPassantSquare << "\n";
                if (to == enPassantSquare)
                {
                    // std::cout << "ADDING EN PASSANT\n";
                    moveList[count] = Move(from, to, 0b0101); // en passant status bitwise
                }
                else if ((0x00000000000000ff & (1ULL << to)) && ((1ULL << to) & occupancies[0]))
                { // If last row for white pawns(promotion)
                    moveList[count] = Move(from, to, 0b1100);
                    count++;
                    moveList[count] = Move(from, to, 0b1101);
                    count++;
                    moveList[count] = Move(from, to, 0b1110);
                    count++;
                    moveList[count] = Move(from, to, 0b1111);
                }
                else
                {
                    moveList[count] = Move(from, to, 0b0100);
                }
                count++;
            }
            pseudolegal = pawn_push[1][from] & (~occupancies[2]);
            while (pseudolegal)
            {
                int to = pop_lsb_bb(pseudolegal);
                //  Regular Push
                if (0x00000000000000ff & (1ULL << to))
                {
                    moveList[count] = Move(from, to, 0b1000);
                    count++;
                    moveList[count] = Move(from, to, 0b1001);
                    count++;
                    moveList[count] = Move(from, to, 0b1010);
                    count++;
                    moveList[count] = Move(from, to, 0b1011);
                }
                else
                {
                    moveList[count] = Move(from, to, 0b0000);
                }
                count++;
            }
            if ((1ULL << from) & 0xff000000000000)
            {
                pseudolegal = pawn_double_push[1][from] & (~occupancies[2]);
                while (pseudolegal)
                {
                    int to = pop_lsb_bb(pseudolegal);
                    //  Double Push
                    if (((pawn_double_push[1][from] | pawn_push[1][from]) & occupancies[2]) == 0)
                    {
                        moveList[count] = Move(from, to, 0b0001);
                        count++;
                    }
                }
            }
        }

        //  Black knights
        from_bb = bbs[7];
        while (from_bb > 0)
        {
            int from = pop_lsb_bb(from_bb);
            uint64_t pseudolegal = knight_masks[from] & (~occupancies[1]);
            while (pseudolegal > 0)
            {
                int to = pop_lsb_bb(pseudolegal);
                if ((1ULL << to) & occupancies[0])
                {
                    moveList[count] = Move(from, to, 0b0100);
                }
                else
                {
                    moveList[count] = Move(from, to, 0b0000);
                }
                count++;
            }
        }

        // Black bishops
        from_bb = bbs[8];
        while (from_bb > 0)
        {
            int from = pop_lsb_bb(from_bb);
            uint64_t pseudolegal = get_bishop_attacks(from, occupancies[2]) & ~occupancies[1];
            while (pseudolegal > 0)
            {
                int to = pop_lsb_bb(pseudolegal);
                if ((1ULL << to) & occupancies[0])
                {
                    moveList[count] = Move(from, to, 0b0100);
                }
                else
                {
                    moveList[count] = Move(from, to, 0b0000);
                }
                count++;
            }
        }

        // Black rooks
        from_bb = bbs[9];
        while (from_bb > 0)
        {
            int from = pop_lsb_bb(from_bb);
            uint64_t pseudolegal = get_rook_attacks(from, occupancies[2]) & ~occupancies[1];
            while (pseudolegal > 0)
            {
                int to = pop_lsb_bb(pseudolegal);
                if ((1ULL << to) & occupancies[0])
                {
                    moveList[count] = Move(from, to, 0b0100);
                }
                else
                {
                    moveList[count] = Move(from, to, 0b0000);
                }
                count++;
            }
        }

        // Black queens
        from_bb = bbs[10];
        while (from_bb > 0)
        {
            int from = pop_lsb_bb(from_bb);
            uint64_t pseudolegal = (get_bishop_attacks(from, occupancies[2]) | get_rook_attacks(from, occupancies[2])) & ~occupancies[1];
            while (pseudolegal > 0)
            {
                int to = pop_lsb_bb(pseudolegal);
                if ((1ULL << to) & occupancies[0])
                {
                    moveList[count] = Move(from, to, 0b0100);
                }
                else
                {
                    moveList[count] = Move(from, to, 0b0000);
                }
                count++;
            }
        }

        //  Black kings
        from_bb = bbs[11];
        while (from_bb > 0)
        {
            int from = pop_lsb_bb(from_bb);
            uint64_t pseudolegal = king_masks[from] & ~occupancies[1];
            while (pseudolegal > 0)
            {
                int to = pop_lsb_bb(pseudolegal);
                if ((1ULL << to) & occupancies[0])
                {
                    moveList[count] = Move(from, to, 0b0100);
                }
                else
                {
                    moveList[count] = Move(from, to, 0b0000);
                }
                count++;
            }
            // Queen side castling
            if (!isSquareAttacked(from - 1, 0) && (castlingRights & 0b0001) && !isSquareAttacked(from, 0))
            {
                if (((7ULL << 57) & (occupancies[2])) == 0)
                {
                    moveList[count] = Move(from, from - 2, 0b0011);
                    count++;
                }
            }
            // King side castling
            if (!isSquareAttacked(from + 1, 0) && (castlingRights & 0b0010) && !isSquareAttacked(from, 0))
            {
                if (((3ULL << 61) & (occupancies[2])) == 0)
                {
                    moveList[count] = Move(from, from + 2, 0b0010);
                    count++;
                }
            }
        }
    }
    moveList[count].data = 0;
    undoList[count] = Undo();
    return count - offset;
}

int Board::generateCaptures(const int offset)
{
    int count = offset;
    uint64_t from_bb;

    if (!sideToMove)
    {
        //
        //  WHITE PIECES MOVE GENERATION
        //

        // White pawns
        from_bb = bbs[0];
        while (from_bb > 0)
        {
            int from = pop_lsb_bb(from_bb);
            uint64_t pseudolegal = pawn_masks[0][from] & (occupancies[1] | ((enPassantSquare != -1) ? (1ULL << enPassantSquare) : 0ULL));
            while (pseudolegal > 0)
            {
                int to = pop_lsb_bb(pseudolegal);

                //  En passant
                // std::cout << "to: " << to << " enPassantSquare: " << enPassantSquare << "\n";
                if (to == enPassantSquare)
                {
                    // std::cout << "ADDING EN PASSANT\n";
                    moveList[count] = Move(from, to, 0b0101); // en passant status bitwise
                }
                else if ((0xff00000000000000 & (1ULL << to)) && ((1ULL << to) & occupancies[1]))
                { // If last row for white pawns(promotion)
                    moveList[count] = Move(from, to, 0b1100);
                    count++;
                    moveList[count] = Move(from, to, 0b1101);
                    count++;
                    moveList[count] = Move(from, to, 0b1110);
                    count++;
                    moveList[count] = Move(from, to, 0b1111);
                }
                else
                {
                    moveList[count] = Move(from, to, 0b0100);
                }
                // std::cout << "Added move " << Move(from, to, 0b0000);

                count++;
            }
        }

        //  White knights
        from_bb = bbs[1];
        while (from_bb > 0)
        {
            int from = pop_lsb_bb(from_bb);
            uint64_t pseudolegal = knight_masks[from] & (~occupancies[0]);
            while (pseudolegal > 0)
            {
                int to = pop_lsb_bb(pseudolegal);
                if ((1ULL << to) & occupancies[1])
                {
                    moveList[count] = Move(from, to, 0b0100);
                    count++;
                }
            }
        }

        // White bishops
        from_bb = bbs[2];
        while (from_bb > 0)
        {
            int from = pop_lsb_bb(from_bb);
            uint64_t pseudolegal = get_bishop_attacks(from, occupancies[2]) & ~occupancies[0];
            while (pseudolegal > 0)
            {
                int to = pop_lsb_bb(pseudolegal);
                if ((1ULL << to) & occupancies[1])
                {
                    moveList[count] = Move(from, to, 0b0100);
                    count++;
                }
            }
        }

        // White rooks
        from_bb = bbs[3];
        while (from_bb > 0)
        {
            int from = pop_lsb_bb(from_bb);
            uint64_t pseudolegal = get_rook_attacks(from, occupancies[2]) & ~occupancies[0];
            while (pseudolegal > 0)
            {
                int to = pop_lsb_bb(pseudolegal);
                if ((1ULL << to) & occupancies[1])
                {
                    moveList[count] = Move(from, to, 0b0100);
                    count++;
                }
            }
        }

        // White queens
        from_bb = bbs[4];
        while (from_bb > 0)
        {
            int from = pop_lsb_bb(from_bb);
            uint64_t pseudolegal = (get_bishop_attacks(from, occupancies[2]) | get_rook_attacks(from, occupancies[2])) & ~occupancies[0];
            while (pseudolegal > 0)
            {
                int to = pop_lsb_bb(pseudolegal);
                if ((1ULL << to) & occupancies[1])
                {
                    moveList[count] = Move(from, to, 0b0100);
                    count++;
                }
            }
        }

        // White kings
        from_bb = bbs[5];
        while (from_bb > 0)
        {
            int from = pop_lsb_bb(from_bb);
            uint64_t pseudolegal = king_masks[from] & ~occupancies[0];
            while (pseudolegal > 0)
            {
                int to = pop_lsb_bb(pseudolegal);
                if ((1ULL << to) & occupancies[1])
                {
                    moveList[count] = Move(from, to, 0b0100);
                    count++;
                }
            }
        }
    }
    else
    {

        //
        //  BLACK PIECES MOVE GENERATION
        //

        // Black pawns

        from_bb = bbs[6];
        while (from_bb > 0)
        {

            int from = pop_lsb_bb(from_bb);
            uint64_t pseudolegal = pawn_masks[1][from] & (occupancies[0] | ((enPassantSquare != -1) ? (1ULL << enPassantSquare) : 0ULL));
            while (pseudolegal > 0)
            {
                int to = pop_lsb_bb(pseudolegal);
                // std::cout << "to: " << to << " enPassantSquare: " << enPassantSquare << "\n";
                if (to == enPassantSquare)
                {
                    // std::cout << "ADDING EN PASSANT\n";
                    moveList[count] = Move(from, to, 0b0101); // en passant status bitwise
                }
                else if ((0x00000000000000ff & (1ULL << to)) && ((1ULL << to) & occupancies[0]))
                { // If last row for white pawns(promotion)
                    moveList[count] = Move(from, to, 0b1100);
                    count++;
                    moveList[count] = Move(from, to, 0b1101);
                    count++;
                    moveList[count] = Move(from, to, 0b1110);
                    count++;
                    moveList[count] = Move(from, to, 0b1111);
                }
                else
                {
                    moveList[count] = Move(from, to, 0b0100);
                }
                count++;
            }
        }

        //  Black knights
        from_bb = bbs[7];
        while (from_bb > 0)
        {
            int from = pop_lsb_bb(from_bb);
            uint64_t pseudolegal = knight_masks[from] & (~occupancies[1]);
            while (pseudolegal > 0)
            {
                int to = pop_lsb_bb(pseudolegal);
                if ((1ULL << to) & occupancies[0])
                {
                    moveList[count] = Move(from, to, 0b0100);
                    count++;
                }
            }
        }

        // Black bishops
        from_bb = bbs[8];
        while (from_bb > 0)
        {
            int from = pop_lsb_bb(from_bb);
            uint64_t pseudolegal = get_bishop_attacks(from, occupancies[2]) & ~occupancies[1];
            while (pseudolegal > 0)
            {
                int to = pop_lsb_bb(pseudolegal);
                if ((1ULL << to) & occupancies[0])
                {
                    moveList[count] = Move(from, to, 0b0100);
                    count++;
                }
            }
        }

        // Black rooks
        from_bb = bbs[9];
        while (from_bb > 0)
        {
            int from = pop_lsb_bb(from_bb);
            uint64_t pseudolegal = get_rook_attacks(from, occupancies[2]) & ~occupancies[1];
            while (pseudolegal > 0)
            {
                int to = pop_lsb_bb(pseudolegal);
                if ((1ULL << to) & occupancies[0])
                {
                    moveList[count] = Move(from, to, 0b0100);
                    count++;
                }
            }
        }

        // Black queens
        from_bb = bbs[10];
        while (from_bb > 0)
        {
            int from = pop_lsb_bb(from_bb);
            uint64_t pseudolegal = (get_bishop_attacks(from, occupancies[2]) | get_rook_attacks(from, occupancies[2])) & ~occupancies[1];
            while (pseudolegal > 0)
            {
                int to = pop_lsb_bb(pseudolegal);
                if ((1ULL << to) & occupancies[0])
                {
                    moveList[count] = Move(from, to, 0b0100);
                    count++;
                }
            }
        }

        //  Black kings
        from_bb = bbs[11];
        while (from_bb > 0)
        {
            int from = pop_lsb_bb(from_bb);
            uint64_t pseudolegal = king_masks[from] & ~occupancies[1];
            while (pseudolegal > 0)
            {
                int to = pop_lsb_bb(pseudolegal);
                if ((1ULL << to) & occupancies[0])
                {
                    moveList[count] = Move(from, to, 0b0100);
                    count++;
                }
            }
        }
    }
    moveList[count].data = 0;
    undoList[count] = Undo();
    return count - offset;
}

void Board::generateKnightMoves()
{
    for (int i = 0; i < 64; i++)
    {
        knight_masks[i] = mask_knight_attacks(i);
    }
}
void Board::generateKingMoves()
{
    for (int i = 0; i < 64; i++)
    {
        king_masks[i] = mask_king_attacks(i);
    }
}
void Board::generatePawnMoves()
{
    for (int i = 0; i < 64; i++)
    {
        pawn_masks[0][i] = mask_pawn_attacks(0, i);
        pawn_masks[1][i] = mask_pawn_attacks(1, i);
        pawn_push[0][i] = mask_pawn_push(0, i, 1);
        pawn_push[1][i] = mask_pawn_push(1, i, 1);
        pawn_double_push[0][i] = mask_pawn_push(0, i, 2);
        pawn_double_push[1][i] = mask_pawn_push(1, i, 2);
    }
}

uint64_t Board::get_bishop_attacks(int square, uint64_t board_occupancy)
{
    board_occupancy &= bishop_masks[square];

    int index = (board_occupancy * BISHOP_MAGICS[square]) >> (64 - bishop_relevant_bits[square]);
    return bishop_attacks[square][index];
}

uint64_t Board::get_rook_attacks(int square, uint64_t board_occupancy)
{
    board_occupancy &= rook_masks[square];

    int index = (board_occupancy * ROOK_MAGICS[square]) >> (64 - rook_relevant_bits[square]);
    return rook_attacks[square][index];
}

void Board::generateBishopMoves()
{
    for (int i = 0; i < 64; i++)
    {
        bishop_masks[i] = mask_bishop_attacks(i);
        bishop_relevant_bits[i] = popcount(bishop_masks[i]);

        int occupancy_count = 1 << bishop_relevant_bits[i];

        // Generating all occupancies and attacks
        for (int j = 0; j < occupancy_count; j++)
        {
            uint64_t occupancy = set_occupancy(j, bishop_relevant_bits[i], bishop_masks[i]);

            // Computing exact attacks for this occupancy (smhw related to ray tracing)
            uint64_t attacks = bishop_attacks_from_occupancy(i, occupancy);

            // Filling magic attack table
            int magic_index = (occupancy * BISHOP_MAGICS[i]) >> (64 - bishop_relevant_bits[i]);
            bishop_attacks[i][magic_index] = attacks;
        }
    }
}

void Board::generateRookMoves()
{
    for (int i = 0; i < 64; i++)
    {
        // gen all attack masks and relevant bits
        rook_masks[i] = mask_rook_attacks(i);
        rook_relevant_bits[i] = popcount(rook_masks[i]);

        int occupancy_count = 1 << rook_relevant_bits[i];

        for (uint64_t j = 0; j < occupancy_count; j++)
        {
            uint64_t occupancy = set_occupancy(j, rook_relevant_bits[i], rook_masks[i]);
            // std::cout << "occupancy\n";
            uint64_t attacks = rook_attacks_from_occupancy(i, occupancy);
            // std::cout << "attacks\n";

            int magic_index = (occupancy * ROOK_MAGICS[i]) >> (64 - rook_relevant_bits[i]);
            // std::cout << "magic index\n";
            rook_attacks[i][magic_index] = attacks;
            if (i == 0 && j == 72339069014639102)
                std::cout << "rook attacks " << attacks << "\n";
        }
        // std::cout << "successfully did i=" << i << "\n";
    }
}

// Make/undo move logic

void Board::makeMove(const Move m, Undo &u)
{
    // int p = findPiece(m.from(), m); // bb index of piece that is getting moved
    int p = mailbox[m.from()];

    int oldCastlingRights = castlingRights;
    // Move piece from source to target.
    int pc = -1; // Piece captured index bb
    clearBit(bbs[p], m.from());
    mailbox[m.from()] = -1;

    if (m.isEnPassant())
    {
        // std::cout << "en passant\n";
        pc = (sideToMove ? 0 : 6); // White to move -> piece captured is black pawn
        clearBit(
            bbs[sideToMove ? 0 : 6], // if white
            (m.to() + (sideToMove ? +8 : -8)));
        mailbox[m.to() + (sideToMove ? +8 : -8)] = -1;
    }
    else if (m.isCapture())
    {
        // pc = findPiece(m.to(), m);
        pc = mailbox[m.to()];

        clearBit(bbs[pc], m.to());
        mailbox[m.to()] = -1;
        if (pc == 3 && m.to() == 0)
        {
            castlingRights &= 0b1011;
        }
        else if (pc == 3 && m.to() == 7)
        {
            castlingRights &= 0b0111;
        }
        else if (pc == 9 && m.to() == 56)
        {
            castlingRights &= 0b1110;
        }
        else if (pc == 9 && m.to() == 63)
        {
            castlingRights &= 0b1101;
        }
    }

    //  Promotion
    if (!m.isPromotion()) // If it isn't promotion, same piece just gets moved to new square
    {
        setBit(bbs[p], m.to());
        mailbox[m.to()] = p;
    }
    else // If it is promotion, new piece gets moved to new square
    {
        int promotedStatus = m.status();
        promotedStatus &= 0x3;
        if (promotedStatus == 3)
        {
            promotedStatus = 4;
        }
        else if (promotedStatus == 2)
        {
            promotedStatus = 3;
        }
        else if (promotedStatus == 1)
        {
            promotedStatus = 2;
        }
        else
        {
            promotedStatus = 1;
        }

        setBit(bbs[promotedStatus + (sideToMove ? +6 : +0)], m.to());
        mailbox[m.to()] = promotedStatus + (sideToMove ? +6 : +0);
    }

    //  Castling
    if (m.status() == 0b0010 || m.status() == 0b0011)
    {
        int kingNew = get_lsb_index(bbs[5 + (sideToMove ? 6 : 0)]);
        if (m.status() == 0b0010)
        {                                                         // King castle
            clearBit(bbs[3 + (sideToMove ? 6 : 0)], kingNew + 1); //   Remove rook from old position
            mailbox[kingNew + 1] = -1;

            setBit(bbs[3 + (sideToMove ? 6 : 0)], kingNew - 1); //  Add rook to the new position
            mailbox[kingNew - 1] = 3 + (sideToMove ? 6 : 0);
        }
        else
        {                                                         // Queen castling
            clearBit(bbs[3 + (sideToMove ? 6 : 0)], kingNew - 2); //  Remove rook from old pos
            mailbox[kingNew - 2] = -1;

            setBit(bbs[3 + (sideToMove ? 6 : 0)], kingNew + 1); // Add it to new
            mailbox[kingNew + 1] = 3 + (sideToMove ? 6 : 0);
        }
    }

    // Save undo info for full restoration.

    u = Undo(oldCastlingRights, pc, enPassantSquare, halfMoveClock, fullMoveClock);
    // std::cout << "Saved in undo en passant square as " << enPassantSquare << "\n";

    // Update game state
    enPassantSquare = -1;
    if (m.status() == 0b0001)
    {
        enPassantSquare = m.to() + (sideToMove ? +8 : -8);
        // std::cout << "en passant square for move " << moveToCode(m) << " is " << enPassantSquare << "\n";
    }

    // Castling rights
    if (p == 5 || p == 11)
    {
        castlingRights &= (sideToMove ? 0b1100 : 0b0011);
    }

    if ((castlingRights & 0b1100) && (p == 3))
    {
        if (m.from() == 0)
        {
            castlingRights &= 0b1011;
        }
        else if (m.from() == 7)
        {
            castlingRights &= 0b0111;
        }
    }

    if ((castlingRights & 0b0011) && (p == 9))
    {
        if (m.from() == 56)
        {
            castlingRights &= 0b1110;
        }
        else if (m.from() == 63)
        {
            castlingRights &= 0b1101;
        }
    }

    // Halfmove/fullmove clocks
    fullMoveClock++;
    if (p == 0 || p == 6 || m.isCapture())
    {
        halfMoveClock = 0;
    }
    else
    {
        halfMoveClock++;
    }

    // Side to move switch
    sideToMove = !sideToMove;

#ifdef DEBUG
    std::cout << "Running debug version\n";
    moveLog.push_back(m);
    boardLog.push_back(getBB());
#endif

    updateOccupancies();
}
void Board::undoMove(const Move m, Undo &u)
{
    if (u.capturedPiece == 11 || u.capturedPiece == 5)
    {
        std::cout << "King was captured\n";
        std::string KING_CAPTURE_ERROR = "King was captured, game is over!";
        throw std::runtime_error(KING_CAPTURE_ERROR);
    }

    enPassantSquare = u.enPassantSquare;
    castlingRights = u.castlingRights;
    halfMoveClock = u.halfMoveClock;
    fullMoveClock = u.fullMoveClock;

    sideToMove = !sideToMove;

    // int p = findPiece(m.to(), m);
    int p = mailbox[m.to()];

    // clearBit(bbs[p], m.to());
    // mailbox[m.to()] = -1;

    // REMOVE PIECE FROM NEW POSITION
    clearBit(bbs[p], m.to());
    mailbox[m.to()] = -1;

    if (m.status() == 0b0101)
    {
        setBit(bbs[u.capturedPiece], m.to() + (sideToMove ? +8 : -8));
        mailbox[m.to() + (sideToMove ? +8 : -8)] = u.capturedPiece;
    }
    else if (u.capturedPiece != -1)
    {
        // std::cout << "Piece was captured, trying to recover it from the " << u.capturedPiece << "\n";
        setBit(bbs[u.capturedPiece], m.to());
        mailbox[m.to()] = u.capturedPiece;
        // std::cout << "SEt the mailbox to " << mailbox[m.to()] << "\n";
        // displayMailbox();
    }

    // Castling
    if (m.isCastling())
    { // WORKS ONLY BEFORE ORIGINAL PIECE WAS MOVED
        // int king = get_lsb_index(bbs[5 + (sideToMove ? 6 : 0)]);
        // std::cout << "is in fact castling\n";
        if (m.status() == 0b0010)
        {                                                        // King castle
            clearBit(bbs[3 + (sideToMove ? 6 : 0)], m.to() - 1); //   Remove rook from old position
            mailbox[m.to() - 1] = -1;

            setBit(bbs[3 + (sideToMove ? 6 : 0)], m.to() + 1); //  Add rook to the new position
            mailbox[m.to() + 1] = 3 + (sideToMove ? 6 : 0);
        }
        else
        {                                                        // Queen castling
            clearBit(bbs[3 + (sideToMove ? 6 : 0)], m.to() + 1); //  Remove rook from old pos
            mailbox[m.to() + 1] = -1;

            setBit(bbs[3 + (sideToMove ? 6 : 0)], m.to() - 2); // Add it to new
            mailbox[m.to() - 2] = 3 + (sideToMove ? 6 : 0);
        }
    }

    // Handle promotion
    if (!m.isPromotion()) // PUTS ORIGINAL PIECE ON THE OLD POSITION
    {
        setBit(bbs[p], m.from());
        mailbox[m.from()] = p;
    }
    else // if IS promotion, but the pawn back.
    {
        setBit(bbs[sideToMove ? 6 : 0], m.from());
        mailbox[m.from()] = (sideToMove ? 6 : 0);
    }

#ifdef DEBUG
    moveLog.pop_back();
    boardLog.pop_back();
#endif
    updateOccupancies();
}

// Is square attacked function

bool Board::isSquareAttacked(int square, int by)
{
    int color = (by ? 6 : 0);
    uint64_t res1 = knight_masks[square] & bbs[1 + color];
    uint64_t res2 = king_masks[square] & bbs[5 + color];
    uint64_t res3 = pawn_masks[!by][square] & bbs[color];
    uint64_t res4 = get_rook_attacks(square, occupancies[2]) & (bbs[3 + color] | bbs[4 + color]);   // rooks + queens
    uint64_t res5 = get_bishop_attacks(square, occupancies[2]) & (bbs[2 + color] | bbs[4 + color]); // bishops + queens
    return res1 || res2 || res3 || res4 || res5;
}

bool Board::isKingAttacked(int by)
{
    int kingIndex = get_lsb_index(bbs[5 + (by ? 0 : 6)]);
    return isSquareAttacked(kingIndex, by);
}

//
// INITIALIZING
//

void Board::startpos()
{
    setFEN("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
}

void Board::pos(int position)
{
    switch (position)
    {
    case (1):
        startpos();
        break;
    case (2):
        setFEN("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
        break;
    case (3):
        setFEN("8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1 ");
        break;
    case (4):
        setFEN("rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8  ");
        break;
    case (5):
        setFEN("rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8  ");
        break;
    case (6):
        setFEN("r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10 ");
        break;
    }
}

//
//   UCI Protocol
//

void Board::uci_loop()
{
    std::string line, token;

    while (std::getline(std::cin, line))
    {
        std::istringstream iss(line);
        iss >> token;

        if (token == "uci")
        {
            std::cout << "id name chess_engine\n";
            std::cout << "id author Danila\n";

            std::cout << "option name Hash type spin default 16 min 1 max 4096\n";
            std::cout << "option name Threads type spin default 1 min 1 max 64\n";
            std::cout << "option name Move Overhead type spin default 100 min 0 max 5000\n";
            std::cout << "option name SyzygyPath type string default \n";

            std::cout << "uciok" << std::endl;
        }
        else if (token == "isready")
        {
            std::cout << "readyok" << "\n";
        }
        else if (token == "position")
        {
            parse_position(iss);
        }
        else if (token == "go")
        {
            parse_go(iss);
        }
        else if (token == "ucinewgame")
        {
            clean();
        }
        else if (token == "stop")
        {
            timeUp = true;
        }
        else if (token.starts_with("setoption"))
        {
            // if (token.find("Hash") != std::string::npos)
            // {
            //     size_t pos = input.find("value");

            //     if (pos != std::string::npos)
            //         hashSize = std::stoi(input.substr(pos + 6));
            // }

            // else if (input.find("Threads") != string::npos)
            // {
            //     size_t pos = input.find("value");

            //     if (pos != std::string::npos)
            //         threads = std::stoi(input.substr(pos + 6));
            // }

            // else if (input.find("Move Overhead") != std::string::npos)
            // {
            //     size_t pos = input.find("value");

            //     if (pos != std::string::npos)
            //         moveOverhead = std::stoi(input.substr(pos + 6));
            // }
        }
        else if (token == "quit")
        {
            break;
        }
    }
}

void Board::parse_position(std::istream &iss)
{
    std::string token;
    iss >> token;
    if (token == "startpos")
    {
        pos(1);
    }
    else if (token == "fen")
    {
        std::string fen, part;
        // std::getline(iss, fen);
        // fen.erase(0, fen.find_first_not_of(" \t"));
        for (int i = 0; i < 6 && iss >> part; i++)
        {
            fen += part + " ";
        }
        // std::cout << fen << "\n";
        // std::cout << "trying to parse fen\n";
        setFEN(fen);
        // std::cout << "parsed fen\n";
    }
    else if (token == "0" || token == "1" || token == "2" || token == "3" || token == "4" || token == "5" || token == "6")
    {
        int n = std::stoi(token);
        pos(n);
    }
    std::string m;
    iss >> m;
    if (m == "moves")
    {
        // std::cout << "trying to parse moves\n";
        parse_moves(iss);
    }
}

void Board::parse_moves(std::istream &iss)
{
    std::string move;
    while (iss >> move)
    {
        Move m = codeToMove(move);
        Undo u; // dummy undo
        makeMove(m, u);
    }
}

void Board::parse_go(std::istream &iss)
{
    std::string token;

    while (iss >> token)
    {
        if (token == "depth")
        {
            int depth;
            iss >> depth;
            timeLimitMs = 999'999;
            Move bestMove = root_search(depth);
            std::cout << "bestmove " << moveToCode(bestMove) << "\n";
        }
        else if (token == "movetime")
        {
            iss >> timeLimitMs;
            start_game();
        }
        else if (token == "perft")
        {
            int depth;
            iss >> depth;
            auto start = std::chrono::steady_clock::now();
            uint64_t nodes = perftDivide(depth);
            auto end = std::chrono::steady_clock::now();

            std::chrono::duration<double> time = end - start;
            double seconds = time.count();

            std::cout << "Total nodes: " << nodes << "\n";
            std::cout << "It took " << seconds << "\n";
            std::cout << "NPS: " << (nodes / seconds) << "\n";
        }
        else if (token == "wtime")
        {
            parse_time(iss);
        }
    }
}

void Board::parse_time(std::istream &iss)
{
    // std::vector<std::string> vec;
    // std::string temp;
    // while (iss >> temp)
    // {
    //     vec.push_back(temp);
    // }
    // int fullTimeWhite = std::stoi(vec[0]);
    // int fullTimeBlack = std::stoi(vec[2]);
    // int incTimeWhite = std::stoi(vec[4]);
    // int incTimeBlack = std::stoi(vec[6]);
    // int myTime = sideToMove ? fullTimeBlack : fullTimeWhite;
    // int myInc = sideToMove ? incTimeBlack : incTimeWhite;
    // timeLimitMs = (myTime / 20) + (myInc / 2);
    timeLimitMs = 5000;
    start_game();
}

void Board::start_game()
{
    timeUp = false;
    nodes = 0;
    startClock = std::chrono::steady_clock::now();
    Move bestMove = moveList[0];
    for (int d = 1; d < 999; d++)
    {
        Move cand = root_search(d);
        if (!timeUp)
        {
            bestMove = cand;
        }
        else
        {
            break;
        }
    }
    std::cout << "bestmove " << moveToCode(bestMove) << "\n";
}

//
//  TESTING
//

int Board::perft(int depth)
{
    if (depth == 0)
    {
        return 1;
    }
    // std::cout << "STARTING PERFT WITH DEPTH " << depth << "\n";
    int nodes = 0;

    int offset = ply * MAX_MOVES;

    generateMoves(offset);
    // std::cout << "Finished generating moves:";
    // displayMoves(moveList);
    for (int i = offset; i < (offset + MAX_MOVES); i++)
    {
        // std::cout << "Started looping over moves...\n";
        if (moveList[i].data == 0)
        {
            break;
        }

#ifdef DEBUG
        uint64_t currentbb = getBB();
        uint64_t currentbb7 = bbs[7];
        std::array<int, 64> copyMailbox = mailbox;
#endif

        ply++;
        // std::cout << "Started making move " << moveList[i] << " and undo " << undoList[i] << "\n";
        makeMove(moveList[i], undoList[i]);
        // std::cout << "Made move " << moveList[i] << " and undo " << undoList[i] << "\n";

#ifdef DEBUG
        std::array<int, 64> copyMailbox2 = mailbox;
        validateBoard(1);
#endif

        if (!isKingAttacked(sideToMove))
        {
            nodes += perft(depth - 1);
        }
        undoMove(moveList[i], undoList[i]);
        ply--;

#ifdef DEBUG

        if (mailbox != copyMailbox)
        {
            std::cout << "Mailbox mismatch for move " << moveList[i] << " and undo " << undoList[i] << "\n";
            std::cout << "mailbox before making move:\n";
            displayMailbox(copyMailbox);
            std::cout << "mailbox after making move:\n";
            displayMailbox(copyMailbox2);
            std::cout << "mailbox after unmaking move:\n";
            displayMailbox();
            std::cout << "\n\n\n\n";
            std::cout << "Move sequence that led to this error position: ";
            for (Move m : moveLog)
            {
                std::cout << moveToCode(m) << ", ";
            }
            std::cout << "\n";
            for (auto i = 0; i < moveLog.size(); i++)
            {
                std::cout << moveToCode(moveLog[i]) << ", and corresponding board:\n";
                displayBB(boardLog[i]);
            }
            std::cout << "\n";
            throw std::runtime_error("Mailbox mismatch\n");
        }
        // std::cout << "undid move " << moveList[i] << " and undo " << undoList[i] << "\n";

        if (currentbb7 != bbs[7])
        {
            std::cout << "\nERROR, black knights. BB at the beginnging: \n";
            displayBB(currentbb7);
            std::cout << "BB at the end: \n";
            displayBB(bbs[7]);
            std::cout << "Crash with the move " << moveList[i] << undoList[i] << "\n";
            std::cout << "\n\n\n\n";
            std::cout << "Move sequence that led to this error position: ";
            for (Move m : moveLog)
            {
                std::cout << moveToCode(m) << ", ";
            }
            std::cout << "\n";
            for (auto i = 0; i < moveLog.size(); i++)
            {
                std::cout << moveToCode(moveLog[i]) << ", and corresponding board:\n";
                displayBB(boardLog[i]);
            }
            std::cout << "\n";
            throw std::runtime_error("Crash of bbs(7) mismatch");
        }
        if (currentbb != getBB())
        {
            std::cout << "\n\n ERROR, BITBOARD MISMATCH. BB at the beginnging: \n";
            displayBB(currentbb);
            std::cout << "BB at the end: \n";
            displayBB(getBB());
            std::cout << "Crash with the move " << moveList[i] << undoList[i] << "\n";
            std::cout << "\n\n\n\n";
            std::cout << "Move sequence that led to this error position: ";
            for (Move m : moveLog)
            {
                std::cout << moveToCode(m) << ", ";
            }
            std::cout << "\n";
            for (auto i = 0; i < moveLog.size(); i++)
            {
                std::cout << moveToCode(moveLog[i]) << ", and corresponding board:\n";
                displayBB(boardLog[i]);
            }
            std::cout << "\n";
            throw std::runtime_error("Crash of bbs mismatch");
        }
        validateBoard(2);
// std::cout << "4 unmade move\n";
#endif
    }
    return nodes;
}

uint64_t Board::perftDivide(int depth)
{
    std::cout << "Trying to run new perft...\n";
    uint64_t totalNodes = 0;
    if (depth == 0)
    {
        return 1;
    }

    int offset = ply * MAX_MOVES;
    generateMoves(offset);

    // std::cout << "Finished generating moves\n";
    for (int i = offset; i < (offset + MAX_MOVES); i++)
    {

        if (moveList[i].data == 0)
        {
            break;
        }

#ifdef DEBUG
        uint64_t currentbb = getBB();
        uint64_t currentbb7 = bbs[7];
#endif
        // std::cout << "Started making move " << moveList[i] << " and undo " << undoList[i] << "\n";

        ply++;
        makeMove(moveList[i], undoList[i]);

#ifdef DEBUG
        validateBoard(3);
#endif
        // std::cout << "2 made move\n";
        // std::cout << "Made move " << moveList[i] << " and undo " << undoList[i] << "\n";
        if (!isKingAttacked(sideToMove))
        {
            // std::cout << "current depth is " << depth << " and condition 'is king attacked' run successfully for a move " << moveList[i] << " and undo " << undoList[i] << "\n";
            uint64_t nodes = perft(depth - 1);
            std::cout << moveToCode(moveList[i]) << ": " << nodes << "\n";
            totalNodes += nodes;
        }
        // std::cout << "Doing perft on a lower level with a move " << moveList[i] << " and undo " << undoList[i] << "\n";
        //  std::cout << "3\n";

        // std::cout << "Trying to undo move " << moveList[i] << " and undo " << undoList[i] << "\n";
        // std::cout << "Started undoing move (divide)" << moveList[i] << " and undo " << undoList[i] << "\n";

        undoMove(moveList[i], undoList[i]);
        ply--;
        // std::cout << "undid move " << moveList[i] << " and undo " << undoList[i] << "\n";

#ifdef DEBUG
        validateBoard(4);

        if (currentbb7 != bbs[7])
        {
            std::cout << "\nERROR, black knights. BB at the beginnging: \n";
            displayBB(currentbb7);
            std::cout << "BB at the end: \n";
            displayBB(bbs[7]);
            std::cout << "Crash with the move " << moveList[i] << undoList[i] << "\n";
            std::cout << "\n\n\n\n";
            std::cout << "Move sequence that led to this error position: ";
            for (Move m : moveLog)
            {
                std::cout << moveToCode(m) << ", ";
            }
            std::cout << "\n";
            for (auto i = 0; i < moveLog.size(); i++)
            {
                std::cout << moveToCode(moveLog[i]) << ", and corresponding board:\n";
                displayBB(boardLog[i]);
            }
            std::cout << "\n";
            throw std::runtime_error("Crash of bbs(7) mismatch");
        }
        if (currentbb != getBB())
        {
            std::cout << "\n\n ERROR, BITBOARD MISMATCH. BB at the beginnging: \n";
            displayBB(currentbb);
            std::cout << "BB at the end: \n";
            displayBB(getBB());
            std::cout << "Piece captured: " << undoList[i].capturedPiece << ". The move itself: " << moveList[i] << " undo itself: " << undoList[i] << "\n";
            throw std::runtime_error("Crash of bbs mismatch");
        }
#endif
    }
    std::cout << "\n Total nodes at depth " << depth << ": " << totalNodes << "\n";
    return totalNodes;
}

void Board::run_perft(int position, int depth)
{
    // int position{1};
    // int depth{1};

    // std::cout << "Default position you want to test(1-6):\n";
    // std::cin >> position;
    // std::cout << "Perft depth:\n";
    // std::cin >> depth;

    switch (position)
    {
    case (1):
        startpos();
        break;
    case (2):
        setFEN("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
        break;
    case (3):
        setFEN("8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1 ");
        break;
    case (4):
        setFEN("rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8  ");
        break;
    case (5):
        setFEN("rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8  ");
        break;
    case (6):
        setFEN("r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10 ");
        break;
    }

    displayBoard();

    displayMailbox();

    auto start = std::chrono::steady_clock::now();
    uint64_t nodes = perftDivide(depth);
    auto end = std::chrono::steady_clock::now();

    std::chrono::duration<double> elapsed = end - start;
    double seconds = elapsed.count();

    std::cout << "Time: " << seconds << "s\n";
    std::cout << "NPS: " << (nodes / seconds) << "\n";
}

//
//  EVALUATION FUNCTION
//

int Board::evaluate()
{
    int val = 0;
    // for (int m : mailbox)
    for (int i = 0; i < 64; i++)
    {
        if (mailbox[i] == -1)
        {
            continue;
        }
        val += pieceValueMap.find(mailbox[i])->second;

        int k = mailbox[i];

        val += ((k > 5) ? -PST[k - 6][64 - i] : PST[k][i]);

        if (!pieceValueMap.count(mailbox[i]))
        {
            throw std::runtime_error("Didn't find piece in the mailbox(eval function)");
        }
    }
    if (std::abs(val) == INF)
    {
        std::cout << "CRAZY MOVE\n";
        displayMailbox();
    }

    return (sideToMove ? -val : val);
}

//
//  SEARCH
//

int Board::search(int depth, int alpha, int beta)
// Alpha - best score that is already guaranteed to you. beta - the best socre opponent will allow to get
{
    nodes++;
    if ((nodes & 2047) == 0)
    {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - startClock).count();
        if (elapsed > timeLimitMs)
        {
            timeUp = true;
        }
    }
    if (timeUp)
        return 0;

    if (depth == 0)
    {
        return quiescence(alpha, beta);
    }

    int offset = MAX_MOVES * ply;

    int pseudoMoves = generateMoves(offset);
    if (!pseudoMoves)
    {
        return isKingAttacked(!sideToMove) ? -MATE_SCORE + ply : 0;
    }

    scoreMoves(offset, pseudoMoves);
    sortMoves(offset, pseudoMoves);

    int best = -INF;
    bool hasLegalMove = false;

    for (int i = offset; i < offset + MAX_MOVES; i++)
    {

        if (moveList[i].data == 0)
        {
            break;
        }

        ply++;
        makeMove(moveList[i], undoList[i]);

        if (isKingAttacked(sideToMove))
        {
            undoMove(moveList[i], undoList[i]);
            ply--;
            continue;
        }
        hasLegalMove = true;

        int score = -search(depth - 1, -beta, -alpha);

        undoMove(moveList[i], undoList[i]);
        ply--;

        if (score > best)
        {
            best = score;
        }

        if (score >= beta)
        {
            return best;
        }

        if (score > alpha)
        {
            alpha = score;
        }
    }

    if (!hasLegalMove)
    {
        return isKingAttacked(!sideToMove) ? -MATE_SCORE + ply : 0;
    }

    return best;
}

Move Board::root_search(int depth)
{

    int offset = ply * MAX_MOVES;

    generateMoves(offset);

    int best = -INF;
    int alpha = -INF;
    int beta = INF;
    Move bestMove = moveList[offset];

    bool foundLegal = false;
#ifdef DEBUG
    std::cout << "Legal root moves:\n";
#endif

    for (int i = offset; i < (offset + MAX_MOVES); i++)
    {
        if (moveList[i].data == 0)
        {
            break;
        }

        ply++;
        makeMove(moveList[i], undoList[i]);

        if (isKingAttacked(sideToMove))
        {
            undoMove(moveList[i], undoList[i]);
            ply--;
            continue;
        }

        foundLegal = true;
        int score = -search(depth - 1, -beta, -alpha);

#ifdef DEBUG
        std::cout << moveToCode(moveList[i]) << ": " << moveList[i] << " " << undoList[i] << " With score: " << score << "\n";
#endif
        undoMove(moveList[i], undoList[i]);
        ply--;

        if (timeUp)
        {
            return bestMove;
        }

        if (score > best)
        {
#ifdef DEBUG
            std::cout << "found new best move " << moveList[i] << " with score: " << score << "\n";
#endif
            best = score;
            bestMove = moveList[i];
        }
        if (score > alpha)
        {
            alpha = score;
        }
    }

    if (!foundLegal)
    {
        // TODO handle stalemate/checkmate
    }

    return bestMove;
}

int Board::quiescence(int alpha, int beta, int qDepth)
{

    if (timeUp)
        return 0;

    // if (qDepth >= 8)
    // {
    //     return evaluate();
    // }

    int stand_pat = evaluate();
    int best = stand_pat;

    if (stand_pat >= beta)
        return best;
    if (stand_pat > alpha)
        alpha = stand_pat;

    int offset = MAX_MOVES * ply;
    int count = generateCaptures(offset);
    scoreMoves(offset, count);
    sortMoves(offset, count);

    for (int i = offset; i < offset + MAX_MOVES; i++)
    {

        if (moveList[i].data == 0)
        {
            break;
        }

        ply++;
        makeMove(moveList[i], undoList[i]);

        if (isKingAttacked(sideToMove))
        {
            undoMove(moveList[i], undoList[i]);
            ply--;
            continue;
        }

        int score = -quiescence(-beta, -alpha, qDepth + 1);

        undoMove(moveList[i], undoList[i]);
        ply--;

        if (score > best)
            best = score;
        if (score >= beta)
            return best;
        if (score > alpha)
            alpha = score;
    }
    return best;
}

inline int Board::scoreMoveCapture(Move m)
{
    return mvv_lva[mailbox[m.from()] % 6][mailbox[m.to()] % 6];
}

void Board::scoreMoves(int offset, int count)
{
    for (int i = offset; i < offset + count; i++)
    {
        if (moveList[i].isCapture())
        {
            moveScores[i] = scoreMoveCapture(moveList[i]);
        }
        else
        {
            moveScores[i] = 0;
        }
    }
}

void Board::sortMoves(int offset, int count)
{
    for (int i = offset; i < offset + count; i++)
    {
        int best = i;
        for (int j = i + 1; j < offset + count; j++)
        {
            if (moveScores[j] > moveScores[best])
                best = j;
        }
        std::swap(moveList[i], moveList[best]);
        std::swap(moveScores[i], moveScores[best]);
    }
}

// //
// //  TRANSPOSITION TABLES
// //

// void Board::init_zobrist()
// {
//     for (int p = 0; p < 12; p++)
//     {
//         for (int sq = 0; sq < 64; sq++)
//         {
//             zobrist_pieces[p][sq] = rand64();
//         }
//     }

//     for (int i = 0; i < 16; i++)
//     {
//         zobrist_castling[i] = rand64();
//     }

//     for (int f = 0; f < 8; f++)
//     {
//         zobrist_side = rand64();
//     }
// }

// uint64_t Board::compute_hash()
// {
//     uint64_t h = 0;

//     for (int sq = 0; sq < 64; sq++)
//     {
//         if (mailbox[sq] != -1)
//             h ^= zobrist_pieces[mailbox[sq]][sq];
//     }

//     h ^= zobrist_castling[castlingRights];
//     if (enPassantSquare != -1)
//     {
//         h ^= zobrist_ep[enPassantSquare % 8];
//     }

//     if (sideToMove)
//     {
//         h ^= zobrist_side;
//     }

//     return h;
// }