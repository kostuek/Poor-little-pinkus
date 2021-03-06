#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <string.h>
#include "state.h"
#include "search.h"
#include "eval.h"
#include "hash.h"
#include "plp.h"
#include "next.h"
#include "quiescence.h"
#include "make.h"
#include "timectrl.h"

#define NOT_PV 0
#define IS_PV 1
#define NO_NULL 0
#define CAN_NULL 1

search_data_t search;

void search_go(state_t *state, int max_depth)
{
    assert(max_depth > 0);

    memset(&search, 0, sizeof(search_data_t));
    search.in_check[0] = move_is_attacked(state, state->king_idx[state->turn], Flip(state->turn));

    search_iterative(state, max_depth);
}

void search_iterative(state_t *state, int max_depth)
{
    for (search.max_depth = 2; search.max_depth <= max_depth; ++search.max_depth)
    {
        search_ab(state, search.max_depth, 0, -INF, INF, NO_NULL, &search.pv, IS_PV);

        if (timecontrol.verbose)
            timectrl_notify_uci(state);

        if (timectrl_should_halt())
            break;

        /* Since we're using Iterative Deepening, a mate result will always be the fastest mate. */
        if (Abs(search.best_score) >= MATE - MAX_DEPTH)
            break;
    }
}

int search_ab(state_t *state, int depth, int ply, int alpha, int beta, int can_null, pv_t *pv, int is_pv)
{
    int *move, *end;
    int count = 0;
    int legal_moves = 0;
    int best_move = 0;
    int hash_type;
    int moves[100];
    int score = 0;
    int o_alpha = alpha;
    int eval;
    int can_prune;
    int extensions;
    pv_t cur_pv;

    cur_pv.count = 0;

    if (timectrl_should_halt())
        return alpha;

    ++search.visited_nodes;

#ifdef USE_REPETITION
    /* Repetition */
    if (ply > 0 && state_is_repeating(state))
        return 0;
#endif

    /* Hash probe */
#ifdef USE_TT
    if (ply > 0 && (hash_type = hash_probe(state->zobrist, depth, alpha, beta, &score)))
    {
        return score;
    }
#endif

    /* Evaluate */
    if (depth <= 0)
    {
        --search.visited_nodes;

        if (is_pv)
            pv->count = 0;

#ifdef USE_QUIESCENCE
        return quiescence(state, ply, alpha, beta);
#else
        return eval_state(state);
#endif
    }

#ifdef USE_NULL
    /* Null move */
    if (can_null && depth > 2 && !search.in_check[ply])
    {
        int R = 2;
        if (depth > 6)
            R = 3;

        make_null_move(state, ply);

        search.in_check[ply + 1] = 0;
        eval = -search_ab(state, depth - 1 - R, ply + 1, -beta, -beta + 1, NO_NULL, NULL, NOT_PV);

        unmake_null_move(state, ply);

        if (eval >= beta)
            return beta;
    }
#endif

    /* Pruning */
#ifdef USE_PRUNING
    static int prune_margins[] = {0, 150, 200, 400};
    can_prune = (depth < 4 &&
                 !search.in_check[ply] &&
                 eval_quick(state) + prune_margins[depth] <= alpha);
#endif

    /* Move generation */
    hash_type = HASH_ALPHA;

#if defined(USE_TT) && defined(USE_HASH_MOVE)
    search.move_phase[ply] = PHASE_HASH;
#else
    search.move_phase[ply] = PHASE_TACTICAL;
#endif

    while (next_moves(state, moves, &count, ply, depth))
    {
        if (!count)
            continue;

        for (move = moves, end = moves + count; move < end; ++move)
        {
            extensions = 0;

            make_move(state, *move, ply);

            /* Legal position ? */
            if (move_is_attacked(state, state->king_idx[Flip(state->turn)], state->turn))
            {
                unmake_move(state, *move, ply);
                continue;
            }

            search.in_check[ply + 1] = move_is_attacked(state, state->king_idx[state->turn], Flip(state->turn));

#ifdef USE_PRUNING
            /* Pruning */
            if (can_prune &&
                legal_moves &&
                search.move_phase[ply] == PHASE_END &&
                !search.in_check[ply + 1])
            {
                search.pruned_nodes += 1;
                unmake_move(state, *move, ply);
                continue;
            }
#endif

            legal_moves += 1;

#ifdef USE_CHECK_EXTENSION
            /* Check extension */
            if (search.in_check[ply + 1])
                extensions += 1;
#endif
            
            if (depth < 3 || !is_pv || alpha == o_alpha)
            {
                eval = -search_ab(state, depth + extensions - 1, ply + 1, -beta, -alpha, CAN_NULL, &cur_pv, is_pv);
            }
            else
            {
                eval = -search_ab(state, depth + extensions - 1, ply + 1, -alpha - 1, -alpha, CAN_NULL, 0, NOT_PV);
                if (eval > alpha)
                    eval = -search_ab(state, depth + extensions - 1, ply + 1, -beta, -alpha, CAN_NULL, &cur_pv, IS_PV);
            }

            unmake_move(state, *move, ply);

            if (timectrl_should_halt())
                return alpha;

            if (eval > alpha)
            {
                /* Fail high ? */
                if (eval >= beta)
                {
#ifdef USE_TT
                    /* Add hash */
                    hash_add_node(state->zobrist, beta, depth, HASH_BETA, *move);
#endif

#ifdef USE_HISTORY
                    /* Increment history */
                    if (MoveCapture(*move) > KING)
                        search.history[*move & 0x7fff] += depth;
#endif

#ifdef USE_KILLERS
                    /* Add killer */
                    if (!IsCaptureOrPromote(*move) && *move != search.killers[ply][0])
                    {
                        search.killers[ply][1] = search.killers[ply][0];
                        search.killers[ply][0] = *move;
                    }
#endif
                    return beta;
                }

                /* PV node */
                alpha = eval;
                hash_type = HASH_EXACT;

                if (is_pv)
                {
                    memcpy(pv->moves + 1, cur_pv.moves, cur_pv.count * sizeof(int));
                    pv->moves[0] = *move;
                    pv->count = cur_pv.count + 1;
                }

                best_move = *move;

                if (ply == 0)
                {
                    search.best_score = eval;

                    if (timecontrol.verbose)
                        timectrl_notify_uci();
                }
            }
        }
    }

    if (!legal_moves)
        alpha = search.in_check[ply] ? -MATE + ply : -10;

#ifdef USE_TT
    hash_add_node(state->zobrist, alpha, depth, hash_type, best_move);
#endif

#ifdef USE_KILLERS
    if (best_move)
    {
        /* Killer */
        if (!IsCaptureOrPromote(best_move) && best_move != search.killers[ply][0])
        {
            search.killers[ply][1] = search.killers[ply][0];
            search.killers[ply][0] = best_move;
        }
    }
#endif

    return alpha;
}
