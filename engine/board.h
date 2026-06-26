#pragma once
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
#include <chrono>

constexpr int MAX_DEPTH = 256;
constexpr int MAX_MOVES = 256;
constexpr int INF = 10'000'000;
constexpr int MATE_SCORE = 100'000;

// most valuable victim - least valuable attacker
// clang-format off
static const  int mvv_lva[6][6]= {
//  victim: P    N    B    R    Q    K
          { 15,  25,  35,  45,  55,  65 }, // attacker: P
          { 14,  24,  34,  44,  54,  64 }, // attacker: N
          { 13,  23,  33,  43,  53,  63 }, // attacker: B
          { 12,  22,  32,  42,  52,  62 }, // attacker: R
          { 11,  21,  31,  41,  51,  61 }, // attacker: Q
          { 10,  20,  30,  40,  50,  60 }, // attacker: K 
};
// clang-format on

static const std::unordered_map<char, int> pieceMap =
    {
        {'p', 0},
        {'n', 1},
        {'b', 2},
        {'r', 3},
        {'q', 4},
        {'k', 5}};

static const std::unordered_map<int, char> fileMap = {
    {0, 'a'},
    {1, 'b'},
    {2, 'c'},
    {3, 'd'},
    {4, 'e'},
    {5, 'f'},
    {6, 'g'},
    {7, 'h'}};

static const std::unordered_map<int, int> pieceValueMap = {
    {0, 100},
    {1, 300},
    {2, 320},
    {3, 500},
    {4, 900},
    {5, 20000},
    {6, -100},
    {7, -300},
    {8, -320},
    {9, -500},
    {10, -900},
    {11, 20000}};

// Clan
static const int PST[5][64] = {
    {0, 0, 0, 0, 0, 0, 0, 0,
     50, 50, 50, 50, 50, 50, 50, 50,
     10, 10, 20, 30, 30, 20, 10, 10,
     5, 5, 10, 25, 25, 10, 5, 5,
     0, 0, 0, 20, 20, 0, 0, 0,
     5, -5, -10, 0, 0, -10, -5, 5,
     5, 10, 10, -20, -20, 10, 10, 5,
     0, 0, 0, 0, 0, 0, 0, 0},
    {-50, -40, -30, -30, -30, -30, -40, -50,
     -40, -20, 0, 0, 0, 0, -20, -40,
     -30, 0, 10, 15, 15, 10, 0, -30,
     -30, 5, 15, 20, 20, 15, 5, -30,
     -30, 0, 15, 20, 20, 15, 0, -30,
     -30, 5, 10, 15, 15, 10, 5, -30,
     -40, -20, 0, 0, 0, 0, -20, -40,
     -50, -40, -30, -30, -30, -30, -40, -50},
    {-20, -10, -10, -10, -10, -10, -10, -20,
     -10, 0, 0, 0, 0, 0, 0, -10,
     -10, 0, 5, 10, 10, 5, 0, -10,
     -10, 5, 5, 10, 10, 5, 5, -10,
     -10, 0, 10, 10, 10, 10, 0, -10,
     -10, 10, 10, 10, 10, 10, 10, -10,
     -10, 5, 0, 0, 0, 0, 5, -10,
     -20, -10, -10, -10, -10, -10, -10, -20},
    {-10, -10, -10, -10, -10, -10, -10, -10,
     -10, 10, 10, 10, 10, 10, 10, -10,
     -10, 10, 10, 10, 10, 10, 10, -10,
     -10, 10, 10, 10, 10, 10, 10, -10,
     -10, 10, 10, 10, 10, 10, 10, -10,
     -10, 10, 10, 10, 10, 10, 10, -10,
     10, 20, 20, 20, 20, 20, 20, 10,
     0, 0, 10, 20, 20, 10, 0, 0},
    {-20, -10, -10, -5, -5, -10, -10, -20,
     -10, 0, 0, 0, 0, 0, 0, -10,
     -10, 0, 5, 5, 5, 5, 0, -10,
     -5, 0, 5, 5, 5, 5, 0, -5,
     0, 0, 5, 5, 5, 5, 0, -5,
     -10, 5, 5, 5, 5, 5, 0, -10,
     -10, 0, 5, 0, 0, 0, 0, -10,
     -20, -10, -10, -5, -5, -10, -10, -20}};

static const int KNIGHT_PST[64] = {
    -50, -40, -30, -30, -30, -30, -40, -50,
    -40, -20, 0, 0, 0, 0, -20, -40,
    -30, 0, 10, 15, 15, 10, 0, -30,
    -30, 5, 15, 20, 20, 15, 5, -30,
    -30, 0, 15, 20, 20, 15, 0, -30,
    -30, 5, 10, 15, 15, 10, 5, -30,
    -40, -20, 0, 0, 0, 0, -20, -40,
    -50, -40, -30, -30, -30, -30, -40, -50};

static const int BISHOP_PST[64] = {
    -20, -10, -10, -10, -10, -10, -10, -20,
    -10, 0, 0, 0, 0, 0, 0, -10,
    -10, 0, 5, 10, 10, 5, 0, -10,
    -10, 5, 5, 10, 10, 5, 5, -10,
    -10, 0, 10, 10, 10, 10, 0, -10,
    -10, 10, 10, 10, 10, 10, 10, -10,
    -10, 5, 0, 0, 0, 0, 5, -10,
    -20, -10, -10, -10, -10, -10, -10, -20};

static const int ROOK_PST[64] = {
    -10, -10, -10, -10, -10, -10, -10, -10,
    -10, 10, 10, 10, 10, 10, 10, -10,
    -10, 10, 10, 10, 10, 10, 10, -10,
    -10, 10, 10, 10, 10, 10, 10, -10,
    -10, 10, 10, 10, 10, 10, 10, -10,
    -10, 10, 10, 10, 10, 10, 10, -10,
    10, 20, 20, 20, 20, 20, 20, 10,
    0, 0, 10, 20, 20, 10, 0, 0};

static const int QUEEN_PST[64] = {
    -20, -10, -10, -5, -5, -10, -10, -20,
    -10, 0, 0, 0, 0, 0, 0, -10,
    -10, 0, 5, 5, 5, 5, 0, -10,
    -5, 0, 5, 5, 5, 5, 0, -5,
    0, 0, 5, 5, 5, 5, 0, -5,
    -10, 5, 5, 5, 5, 5, 0, -10,
    -10, 0, 5, 0, 0, 0, 0, -10,
    -20, -10, -10, -5, -5, -10, -10, -20};

const static uint64_t BISHOP_MAGICS[] = {10415965774840320, 289360158742577153, 361423018515501088, 1130325870650113, 1140605891706882, 571771950465056, 9656281805703372800, 577027559150723074, 578787387895840904, 288265594891993168, 216177218848948232, 288234894466220418, 432488509601751040, 2242008581504, 20833548538742784, 288515154101035008, 2884625999062960144, 9876535279798782976, 40532439621177600, 2402679208066236420, 144713339591721216, 9259963925572256768, 633464744315200, 4902744342806006016, 171171970279836160, 13983681241065980424, 289395961690604544, 2312624796942336002, 149181875196297280, 577025901550669824, 4613944692638352899, 595602704286370848, 4653150659152896, 158398396302356, 76605178459456004, 4630903284805664896, 2955496060151177224, 4847157439369379968, 14641114015498368, 2306026637405258752, 21691199853273088, 9372063401183830049, 5188164571357024256, 86975906718991106, 9227880038991465473, 668641666269312, 72624954958676992, 2886885435068383264, 10448633194663151888, 583271702987800705, 88100584833344, 4627484181667250194, 9007251084017664, 4757701682303926784, 2260699279794240, 580982498172896001, 565158246351876, 55178467666436114, 4362667072, 9224779411875301377, 12685232776803387904, 602678472278528, 2382439395874702340, 18158469983043616};
const static uint64_t ROOK_MAGICS[] = {9259401108821016592ULL, 18014467231055937, 72067489913110592, 36034947420532736, 36033195199725634, 144117387166320688, 1333101224383218176, 2341872905780478080, 2891592586085302272, 844566806675456, 5764748329311273088, 1162210247558365217, 10376856531882746368, 2814838226027008, 1516024233154085124, 1153202981227758848, 72198881420509216, 45040669200502784, 1267187419521028, 144680337901881408, 141287311292416, 4223224300961930, 13194273886241, 9572348239872068, 598694818824192, 45036274373885984, 281754151682576, 40536804358094912, 144415359045470208, 5068750759920640, 564410242860560, 6917670873232212036, 5908793150863835168, 720787115369046016, 9009694647525392, 141252926377986, 7322861794509202448, 1190076270285815816, 9592672713588082704, 76561760701644873, 1152991892714061824, 4652230785117732864, 585608964226547748, 937032577150156832, 9381006819972907136, 563517024501776, 289637897072607236, 36029099831066628, 288512401454678528, 4665742545555329152, 1301826067579136, 35220074070272, 5228687964074180736, 42977985921550337, 864708725070496768, 576482764154749440, 4689444031338389762, 1450722174938648706, 90142499166752898, 7066174342404571205, 587157420298470434, 281492157104657, 36047523345989764, 9346390164234};

struct Undo
{
    uint8_t castlingRights;
    int capturedPiece;
    int enPassantSquare;
    uint8_t halfMoveClock;
    uint16_t fullMoveClock;
    Undo();
    Undo(uint8_t castlingRights, int capturedPiece, int enPassantSquare, uint8_t halfMoveClock, uint16_t fullMoveClock);
};
struct Move
{

    //  first 6 bits - from, next 6 - to,
    //  2 for type(0-normal, 1-promotion, 2- en passant, 3 -castling),
    //  and 2 for promoted piece type(0 - knight, 1-bishop, 2-rook, 3-queen)

    // NEW WAY - 4 bits for variations:
    // 0000 - quiet
    // 0001 - pawn double push
    // 0010 - king castle
    // 0011 - queen castle
    // 0100 - capture
    // 0101 - en passant
    // 1000 - knight promotion
    // 1001 - bishop promotion
    // 1010 - rook promotion
    // 1011 - queen promotion
    // 1100 - knight promotion + capture
    // 1101 - bishop promotion + capture
    // 1110 - rook promotion + capture
    // 1111 - queen promotion + capture

    uint16_t data;
    Move();
    Move(int from, int to, int status);
    int from() const;
    int to() const;
    int status() const;

    bool isPromotion() const;
    bool isEnPassant() const;
    bool isCastling() const;
    bool isCapture() const;
};

std::ostream &operator<<(std::ostream &os, const Move &obj);

// clang-format off
enum squares : int{
    a1, b1, c1, d1, e1, f1 ,g1, h1,
    a2, b2, c2, d2, e2, f2 ,g2, h2,
    a3, b3, c3, d3, e3, f3 ,g3, h3,
    a4, b4, c4, d4, e4, f4 ,g4, h4,
    a5, b5, c5, d5, e5, f5 ,g5, h5,
    a6, b6, c6, d6, e6, f6 ,g6, h6,
    a7, b7, c7, d7, e7, f7 ,g7, h7,
    a8, b8, c8, d8, e8, f8 ,g8, h8
};
// clang-format on

namespace masks
{
    constexpr uint64_t notAfile = 9259542123273814144;
}

class Board
{
public:
    // a1 = 0, h8 - 63;
    std::array<uint64_t, 12> bbs;
    // 0-5 - white pieces. 0 - pawns, 1 - knights, 2 - bishops, 3 - rooks, 4 - queen, 5 - king
    // 6-11 - black pieces. 6 - pawns, 7 - knights, 8 - bishops, 9 - rooks, 10 - queen, 11 - king
    std::array<uint64_t, 3> occupancies;
    // 0 - all white pieces, 1 - all black pieces, 2 - all pieces

    std::array<int, 64> mailbox;

    uint8_t castlingRights = 0b0000; // 1st - white king, 2nd - white queen, 3rd - black king, 4th - black queen

    int enPassantSquare = -1;

    int halfMoveClock = 0;
    int fullMoveClock = 1;

    int sideToMove = 0;

    std::array<Move, MAX_MOVES * MAX_DEPTH> moveList;
    std::array<Undo, MAX_MOVES * MAX_DEPTH> undoList;
    std::array<int, MAX_MOVES * MAX_DEPTH> moveScores;

    int ply = 0;

    std::chrono::steady_clock::time_point startClock;
    bool timeUp = false;
    uint64_t nodes = 0;
    int timeLimitMs = 0;

    void clean();

#ifdef DEBUG
    std::vector<Move> moveLog;
    std::vector<uint64_t> boardLog;
#endif

    // Bitboard masks with attacks:
    static uint64_t pawn_masks[2][64];
    static uint64_t pawn_push[2][64];
    static uint64_t pawn_double_push[2][64];
    static uint64_t knight_masks[64];
    static uint64_t king_masks[64];
    static uint64_t bishop_masks[64];
    static uint64_t rook_masks[64];

    static uint64_t bishop_relevant_bits[64];
    static uint64_t rook_relevant_bits[64];

    //  Magic bbs
    static uint64_t bishop_attacks[64][512];
    static uint64_t rook_attacks[64][4096];

    // // TT
    // static uint64_t zobrist_pieces[12][64];
    // static uint64_t zobrist_castling[16]; // all combinations of castling
    // static uint64_t zobrist_ep[8];        // en passant file
    // static uint64_t zobrist_side;         // black to move

    // uint64_t hashKey;

    Board();
    void inline setBit(uint64_t &bb, int square);
    void clearBit(uint64_t &bb, int square);
    bool isBitSet(uint64_t bb, int square);
    int pieceOn(int square);

    uint64_t get_lsb_bb(uint64_t bb);
    uint64_t pop_lsb_bb(uint64_t &bb);
    int get_lsb_index(uint64_t bb);
    inline int popcount(uint64_t bb);
    void updateOccupancies();
    int findPiece(int square, Move m = Move());
    int findPieceKing(int square, Move m = Move());
    void validateBoard(int i);
    uint64_t getBB();

    void displayBoard();
    void displayBB(uint64_t bb);
    void displayMoves(std::array<Move, 256> moveList);
    void displayMailbox();
    void displayMailbox(std::array<int, 64> &mb);

    std::vector<std::string> splitString(std::string str, char delimiter);
    int codeToIndex(std::string code);
    std::string indexToCode(int index);
    std::string moveToCode(Move m);
    Move codeToMove(std::string s);
    std::string getFEN();
    void setFEN(std::string s);

    // Magic generation
    uint64_t rand64();
    uint64_t sparse_rand();
    uint64_t find_magic(int sq, bool bishop);
    void searchAllMagics();

    //  Initialization
    uint64_t mask_pawn_attacks(int side, int square);
    uint64_t mask_pawn_push(int side, int square, int push);
    uint64_t mask_knight_attacks(int square);
    uint64_t mask_king_attacks(int square);
    uint64_t mask_bishop_attacks(int square);
    uint64_t mask_rook_attacks(int square);

    uint64_t set_occupancy(int index, int bits, uint64_t mask);
    uint64_t bishop_attacks_from_occupancy(int square, uint64_t blockers);
    uint64_t rook_attacks_from_occupancy(int square, uint64_t blockers);

    uint64_t get_bishop_attacks(int square, uint64_t board_occupancy);
    uint64_t get_rook_attacks(int square, uint64_t board_occupancy);

    int generateMoves(int offset);
    int generateCaptures(const int offset);
    void generateKnightMoves();
    void generateKingMoves();
    void generatePawnMoves();
    void generateBishopMoves();
    void generateRookMoves();

    void makeMove(const Move m, Undo &u);
    void undoMove(const Move m, Undo &u);

    bool isSquareAttacked(int square, int by);
    bool isKingAttacked(int by);

    //  Initializing
    void startpos();
    void pos(int position);

    // UCI protocol
    void uci_loop();
    void parse_position(std::istream &iss);
    void parse_moves(std::istream &iss);
    void parse_go(std::istream &iss);
    void parse_time(std::istream &iss);
    void start_game();

    //  Testing move generation
    int perft(int depth);
    uint64_t perftDivide(int depth);
    void run_perft(int position = 1, int depth = 1);

    //  Evaluation function
    int evaluate();

    // Search
    int search(int depth, int alpha, int beta);
    Move root_search(int depth);
    int quiescence(int alpha, int beta, int qDepth = 0);

    int scoreMoveCapture(Move m);
    void scoreMoves(int offset, int count);
    void sortMoves(int offset, int count);

    // // Transposition tables
    // void init_zobrist();
    // uint64_t compute_hash();
};