#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "hash.h"
#include "defines.h"
#if defined(__LP64__)
    #include "inline64.h"
#else
    #include "inline32.h"
#endif

static hash_node_t *hash_table;
static int _hash_table_size;

static inline uint64_t _rand64()
{
    return rand() ^ ((uint64_t)rand() << 15)
            ^ ((uint64_t)rand() << 30) ^ ((uint64_t)rand() << 45)
            ^ ((uint64_t)rand() << 60);
}

static void _init_zobrist()
{
    /* Initializes the random values we'll use for making zobrist keys.
     * Different srand seeds will give different results */

    int piece, color, idx;
    for (color = WHITE; color <= BLACK; ++color)
    {
        for (piece = PAWN; piece <= KING; ++piece)
        {
            for (idx = 0; idx < 64; ++idx)
            {
                hash_zobrist->pieces[color][piece][idx] = _rand64();
            }
        }
    }

    for (idx = 0; idx < 64; ++idx)
    {
        hash_zobrist->en_passant[idx] = _rand64();
        hash_zobrist->castling[idx] = _rand64();
    }

    hash_zobrist->turn = _rand64();
}


void hash_init(int table_size)
{
    _hash_table_size = table_size;

    hash_zobrist = malloc(sizeof(hash_zobrist_t));

    hash_table = malloc(sizeof(hash_node_t) * _hash_table_size);
    memset(hash_table, 0, sizeof(hash_node_t) * _hash_table_size);

    _init_zobrist();
}

void hash_destroy()
{
    free(hash_table);
    free(hash_zobrist);
}


void hash_add_node(uint64_t zobrist_key, uint64_t score, int depth)
{
    int idx = zobrist_key % _hash_table_size;

    hash_table[idx].hash = zobrist_key;
    hash_table[idx].depth = depth;
    hash_table[idx].score = score;
}

hash_node_t *hash_get_node(uint64_t zobrist_key)
{
    /* The returned node must be checked to see if the zobrist keys are matching. */
    int idx = zobrist_key % _hash_table_size;
    return &hash_table[idx];
}


uint64_t hash_make_zobrist(state_t *state)
{
    /* Makes a zobrist key from scratch, given a state.
     * Should only be called once, when initializing a new state */
    uint64_t ret = 0;

    int color, piece;
    for (color = WHITE; color <= BLACK; ++color)
    {
        for (piece = PAWN; piece <= KING; ++piece)
        {
            uint64_t pieces = state->pieces[color][piece];
            while (pieces)
            {
                int piece_idx = LSB(pieces);
                pieces &= pieces - 1;

                ret ^= hash_zobrist->pieces[color][piece][piece_idx];
            }
        }
    }

    if (state->en_passant)
    {
        int en_passant_idx = LSB(state->en_passant);
        ret ^= hash_zobrist->en_passant[en_passant_idx];
    }


    uint64_t castling = state->castling;
    while (castling)
    {
        int castling_idx = LSB(castling & -castling);
        castling &= castling - 1;

        ret ^= hash_zobrist->castling[castling_idx];
    }

    if (state->turn)
    {
        ret ^= hash_zobrist->turn;
    }

    return ret;
}