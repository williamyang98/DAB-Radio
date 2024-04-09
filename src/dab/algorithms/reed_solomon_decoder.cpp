/* Reed-Solomon decoder
 * Copyright 2002-2004 Phil Karn, KA9Q
 * May be used under the terms of the GNU Lesser General Public License (LGPL)
 */
#include "./reed_solomon_decoder.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
// alloca() for windows
#if _WIN32
#include <malloc.h>
#endif

// Taken from rs-common.h
// Reed-Solomon codec control block
typedef uint8_t data_t;
struct RS_data {
    int mm;           /* Bits per symbol */
    int nn;           /* Symbols per block (= (1<<mm)-1) */
    data_t *alpha_to; /* log lookup table */
    data_t *index_of; /* Antilog lookup table */
    data_t *genpoly;  /* Generator polynomial */
    int nroots;       /* Number of generator roots = number of parity symbols */
    int fcr;          /* First consecutive root, index form */
    int prim;         /* Primitive element, index form */
    int iprim;        /* prim-th root of 1, index form */
    int pad;          /* Padding bytes in shortened block */
};

static struct RS_data *init_rs_char(int symsize, int gfpoly, int fcr, int prim, int nroots, int pad);
static void free_rs_char(struct RS_data *rs);
static int decode_rs_char(struct RS_data* rs, uint8_t *data, int *eras_pos, int no_eras);

// Taken from rs-common.h
static inline int modnn(struct RS_data *rs, int x) {
    while (x >= rs->nn) {
        x -= rs->nn;
        x = (x >> rs->mm) + (x & rs->nn);
    }
    return x;
}

// Helper function to allocate arrays on the stack
// C++ doesn't allow variable sized stack allocation by default unlike C
#define STACK_ALLOC(n) reinterpret_cast<uint8_t*>(alloca(n))

// Helper macros
#undef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#undef A0
#define A0 (NN)

// Macros to shorten the rs struct referencing
#define MODNN(x) modnn(rs, x)
#define MM (rs->mm)
#define NN (rs->nn)
#define ALPHA_TO (rs->alpha_to)
#define INDEX_OF (rs->index_of)
#define GENPOLY (rs->genpoly)
#define NROOTS (rs->nroots)
#define FCR (rs->fcr)
#define PRIM (rs->prim)
#define IPRIM (rs->iprim)
#define PAD (rs->pad)
#define A0 (NN)

// NOLINTBEGIN: clang-tidy really doesn't like this code
// Found in init_rs_char.h and init_rs_char.c with code inlined from init_rs.h
// This is a char only version of init_rs(..)
struct RS_data *init_rs_char(int symsize, int gfpoly, int fcr, int prim, int nroots, int pad) {
    struct RS_data *rs = NULL;

    // Everything past this point is #include "init_rs.h" into the body
    /* Common code for intializing a Reed-Solomon control block (char or int symbols)
    * Copyright 2004 Phil Karn, KA9Q
    * May be used under the terms of the GNU Lesser General Public License (LGPL)
    */

    int i, j, sr, root, iprim;
    /* Check parameter ranges */
    if (symsize < 0 || (size_t)symsize > 8 * sizeof(data_t)) {
        goto done;
    }

    if (fcr < 0 || fcr >= (1 << symsize)) {
        goto done;
    }
    if (prim <= 0 || prim >= (1 << symsize)) {
        goto done;
    }
    if (nroots < 0 || nroots >= (1 << symsize)) {
        goto done; /* Can't have more roots than symbol values! */
    }
    if (pad < 0 || pad >= ((1 << symsize) - 1 - nroots)) {
        goto done; /* Too much padding */
    }

    rs = (struct RS_data *)calloc(1, sizeof(struct RS_data));
    if (rs == NULL) {
        goto done;
    }

    rs->mm = symsize;
    rs->nn = (1 << symsize) - 1;
    rs->pad = pad;

    rs->alpha_to = (data_t *)malloc(sizeof(data_t) * (rs->nn + 1));
    if (rs->alpha_to == NULL) {
        free(rs);
        rs = NULL;
        goto done;
    }
    rs->index_of = (data_t *)malloc(sizeof(data_t) * (rs->nn + 1));
    if (rs->index_of == NULL) {
        free(rs->alpha_to);
        free(rs);
        rs = NULL;
        goto done;
    }

    /* Generate Galois field lookup tables */
    rs->index_of[0] = A0; /* log(zero) = -inf */
    rs->alpha_to[A0] = 0; /* alpha**-inf = 0 */
    sr = 1;
    for (i = 0; i < rs->nn; i++) {
        rs->index_of[sr] = i;
        rs->alpha_to[i] = sr;
        sr <<= 1;
        if (sr & (1 << symsize))
            sr ^= gfpoly;
        sr &= rs->nn;
    }
    if (sr != 1) {
        /* field generator polynomial is not primitive! */
        free(rs->alpha_to);
        free(rs->index_of);
        free(rs);
        rs = NULL;
        goto done;
    }

    /* Form RS code generator polynomial from its roots */
    rs->genpoly = (data_t *)malloc(sizeof(data_t) * (nroots + 1));
    if (rs->genpoly == NULL) {
        free(rs->alpha_to);
        free(rs->index_of);
        free(rs);
        rs = NULL;
        goto done;
    }
    rs->fcr = fcr;
    rs->prim = prim;
    rs->nroots = nroots;

    /* Find prim-th root of 1, used in decoding */
    for (iprim = 1; (iprim % prim) != 0; iprim += rs->nn);
    rs->iprim = iprim / prim;

    rs->genpoly[0] = 1;
    for (i = 0, root = fcr * prim; i < nroots; i++, root += prim) {
        rs->genpoly[i + 1] = 1;
        /* Multiply rs->genpoly[] by  @**(root + x) */
        for (j = i; j > 0; j--) {
            if (rs->genpoly[j] != 0)
                rs->genpoly[j] = rs->genpoly[j - 1] ^ rs->alpha_to[modnn(rs, rs->index_of[rs->genpoly[j]] + root)];
            else
                rs->genpoly[j] = rs->genpoly[j - 1];
        }
        /* rs->genpoly[0] can never be zero */
        rs->genpoly[0] = rs->alpha_to[modnn(rs, rs->index_of[rs->genpoly[0]] + root)];
    }
    /* convert rs->genpoly[] to index form for quicker encoding */
    for (i = 0; i <= nroots; i++) {
        rs->genpoly[i] = rs->index_of[rs->genpoly[i]];
    }
done:
    return rs;
}

// Found in init_rs_char.h and init_rs_char.c
// This is a char only version of free_rs(..)
void free_rs_char(struct RS_data *rs) {
    free(rs->alpha_to);
    free(rs->index_of);
    free(rs->genpoly);
    free(rs);
}

// Found in decode_rs.h and decode_rs_char.c
// decode_rs.h is included inside the decoder_rs_char.c function body to inline the code
// Here we just pasted the code in to keep it in one file
int decode_rs_char(struct RS_data* rs, uint8_t *data, int *eras_pos, int no_eras) {
    int retval;

    // Everything past this point in decoder_rs_char.c is inlined by #include "decoder_rs.h"

    /* The guts of the Reed-Solomon decoder, meant to be #included
    * into a function body with the following typedefs, macros and variables supplied
    * according to the code parameters:
    * data_t - a typedef for the data symbol
    * data_t data[] - array of NN data and parity symbols to be corrected in place
    * retval - an integer lvalue into which the decoder's return code is written
    * NROOTS - the number of roots in the RS code generator polynomial,
    *          which is the same as the number of parity symbols in a block.
                Integer variable or literal.
    * NN - the total number of symbols in a RS block. Integer variable or literal.
    * PAD - the number of pad symbols in a block. Integer variable or literal.
    * ALPHA_TO - The address of an array of NN elements to convert Galois field
    *            elements in index (log) form to polynomial form. Read only.
    * INDEX_OF - The address of an array of NN elements to convert Galois field
    *            elements in polynomial form to index (log) form. Read only.
    * MODNN - a function to reduce its argument modulo NN. May be inline or a macro.
    * FCR - An integer literal or variable specifying the first consecutive root of the
    *       Reed-Solomon generator polynomial. Integer variable or literal.
    * PRIM - The primitive root of the generator poly. Integer variable or literal.
    * DEBUG - If set to 1 or more, do various internal consistency checking. Leave this
    *         undefined for production code
    * The memset(), memmove(), and memcpy() functions are used. The appropriate header
    * file declaring these functions (usually <string.h>) must be included by the calling
    * program.
    */
    int deg_lambda, el, deg_omega;
    int i, j, r, k;
    data_t u, q, tmp, num1, num2, den, discr_r;
    int syn_error, count;

    auto lambda = STACK_ALLOC(NROOTS+1);
    auto s      = STACK_ALLOC(NROOTS);      // Err+Eras locator poly and syndrome poly
    auto b      = STACK_ALLOC(NROOTS+1);
    auto t      = STACK_ALLOC(NROOTS+1);
    auto omega  = STACK_ALLOC(NROOTS+1);
    auto root   = STACK_ALLOC(NROOTS);
    auto reg    = STACK_ALLOC(NROOTS+1);
    auto loc    = STACK_ALLOC(NROOTS);

    /* form the syndromes; i.e., evaluate data(x) at roots of g(x) */
    for (i = 0; i < NROOTS; i++) {
        s[i] = data[0];
    }

    for (j = 1; j < NN - PAD; j++) {
        for (i = 0; i < NROOTS; i++) {
            if (s[i] == 0) {
                s[i] = data[j];
            } else {
                s[i] = data[j] ^ ALPHA_TO[MODNN(INDEX_OF[s[i]] + (FCR + i) * PRIM)];
            }
        }
    }

    /* Convert syndromes to index form, checking for nonzero condition */
    syn_error = 0;
    for (i = 0; i < NROOTS; i++) {
        syn_error |= s[i];
        s[i] = INDEX_OF[s[i]];
    }

    if (!syn_error) {
        /* if syndrome is zero, data[] is a codeword and there are no
         * errors to correct. So return data[] unmodified
         */
        count = 0;
        goto finish;
    }
    memset(&lambda[1], 0, NROOTS * sizeof(lambda[0]));
    lambda[0] = 1;

    if (no_eras > 0) {
        /* Init lambda to be the erasure locator polynomial */
        lambda[1] = ALPHA_TO[MODNN(PRIM * (NN - 1 - eras_pos[0]))];
        for (i = 1; i < no_eras; i++) {
            u = MODNN(PRIM * (NN - 1 - eras_pos[i]));
            for (j = i + 1; j > 0; j--) {
                tmp = INDEX_OF[lambda[j - 1]];
                if (tmp != A0) {
                    lambda[j] ^= ALPHA_TO[MODNN(u + tmp)];
                }
            }
        }

#if DEBUG >= 1
        /* Test code that verifies the erasure locator polynomial just constructed
           Needed only for decoder debugging. */

        /* find roots of the erasure location polynomial */
        for (i = 1; i <= no_eras; i++) {
            reg[i] = INDEX_OF[lambda[i]];
        }

        count = 0;
        for (i = 1, k = IPRIM - 1; i <= NN; i++, k = MODNN(k + IPRIM)) {
            q = 1;
            for (j = 1; j <= no_eras; j++) {
                if (reg[j] != A0) {
                    reg[j] = MODNN(reg[j] + j);
                    q ^= ALPHA_TO[reg[j]];
                }
            }
            if (q != 0) {
                continue;
            }
            /* store root and error location number indices */
            root[count] = i;
            loc[count] = k;
            count++;
        }

        if (count != no_eras) {
            fprintf(stderr, "count = %d no_eras = %d\n lambda(x) is WRONG\n", count, no_eras);
            count = -1;
            goto finish;
        }
#if DEBUG >= 2
        fprintf(stderr, "\n Erasure positions as determined by roots of Eras Loc Poly:\n");
        for (i = 0; i < count; i++) {
            fprintf(stderr, "%d ", loc[i]);
        }
        fprintf(stderr, "\n");
#endif
#endif
    }

    for (i = 0; i < NROOTS + 1; i++) {
        b[i] = INDEX_OF[lambda[i]];
    }

    /*
     * Begin Berlekamp-Massey algorithm to determine error+erasure
     * locator polynomial
     */
    r = no_eras;
    el = no_eras;
    while (++r <= NROOTS) {
        /* r is the step number */
        /* Compute discrepancy at the r-th step in poly-form */
        discr_r = 0;
        for (i = 0; i < r; i++) {
            if ((lambda[i] != 0) && (s[r - i - 1] != A0)) {
                discr_r ^= ALPHA_TO[MODNN(INDEX_OF[lambda[i]] + s[r - i - 1])];
            }
        }
        discr_r = INDEX_OF[discr_r]; /* Index form */
        if (discr_r == A0) {
            /* 2 lines below: B(x) <-- x*B(x) */
            memmove(&b[1], b, NROOTS * sizeof(b[0]));
            b[0] = A0;
        } else {
            /* 7 lines below: T(x) <-- lambda(x) - discr_r*x*b(x) */
            t[0] = lambda[0];
            for (i = 0; i < NROOTS; i++) {
                if (b[i] != A0) {
                    t[i + 1] = lambda[i + 1] ^ ALPHA_TO[MODNN(discr_r + b[i])];
                } else {
                    t[i + 1] = lambda[i + 1];
                }
            }
            if (2 * el <= r + no_eras - 1) {
                el = r + no_eras - el;
                /*
                 * 2 lines below: B(x) <-- inv(discr_r) *
                 * lambda(x)
                 */
                for (i = 0; i <= NROOTS; i++) {
                    b[i] = (lambda[i] == 0) ? A0 : MODNN(INDEX_OF[lambda[i]] - discr_r + NN);
                }
            } else {
                /* 2 lines below: B(x) <-- x*B(x) */
                memmove(&b[1], b, NROOTS * sizeof(b[0]));
                b[0] = A0;
            }
            memcpy(lambda, t, (NROOTS + 1) * sizeof(t[0]));
        }
    }

    /* Convert lambda to index form and compute deg(lambda(x)) */
    deg_lambda = 0;
    for (i = 0; i < NROOTS + 1; i++) {
        lambda[i] = INDEX_OF[lambda[i]];
        if (lambda[i] != A0) {
            deg_lambda = i;
        }
    }

    /* Find roots of the error+erasure locator polynomial by Chien search */
    memcpy(&reg[1], &lambda[1], NROOTS * sizeof(reg[0]));
    count = 0; /* Number of roots of lambda(x) */
    for (i = 1, k = IPRIM - 1; i <= NN; i++, k = MODNN(k + IPRIM)) {
        q = 1; /* lambda[0] is always 0 */
        for (j = deg_lambda; j > 0; j--) {
            if (reg[j] != A0) {
                reg[j] = MODNN(reg[j] + j);
                q ^= ALPHA_TO[reg[j]];
            }
        }
        if (q != 0) {
            continue; /* Not a root */
        }
#if DEBUG >= 2
        fprintf(stderr, "count %d root %d loc %d\n", count, i, k);
#endif
        /* store root (index-form) and error location number */
        root[count] = i;
        loc[count] = k;
        /* If we've already found max possible roots,
         * abort the search to save time
         */
        if (++count == deg_lambda) {
            break;
        }
    }

    if (deg_lambda != count) {
        /*
         * deg(lambda) unequal to number of roots => uncorrectable
         * error detected
         */
        count = -1;
        goto finish;
    }

    /*
     * Compute err+eras evaluator poly omega(x) = s(x)*lambda(x) (modulo
     * x**NROOTS). in index form. Also find deg(omega).
     */
    deg_omega = deg_lambda - 1;
    for (i = 0; i <= deg_omega; i++) {
        tmp = 0;
        for (j = i; j >= 0; j--) {
            if ((s[i - j] != A0) && (lambda[j] != A0)) {
                tmp ^= ALPHA_TO[MODNN(s[i - j] + lambda[j])];
            }
        }
        omega[i] = INDEX_OF[tmp];
    }

    /*
     * Compute error values in poly-form. num1 = omega(inv(X(l))), num2 =
     * inv(X(l))**(FCR-1) and den = lambda_pr(inv(X(l))) all in poly-form
     */
    for (j = count - 1; j >= 0; j--) {
        num1 = 0;
        for (i = deg_omega; i >= 0; i--) {
            if (omega[i] != A0) {
                num1 ^= ALPHA_TO[MODNN(omega[i] + i * root[j])];
            }
        }
        num2 = ALPHA_TO[MODNN(root[j] * (FCR - 1) + NN)];
        den = 0;

        /* lambda[i+1] for i even is the formal derivative lambda_pr of lambda[i] */
        for (i = MIN(deg_lambda, NROOTS - 1) & ~1; i >= 0; i -= 2) {
            if (lambda[i + 1] != A0) {
                den ^= ALPHA_TO[MODNN(lambda[i + 1] + i * root[j])];
            }
        }
#if DEBUG >= 1
        if (den == 0) {
            fprintf(stderr, "\n ERROR: denominator = 0\n");
            count = -1;
            goto finish;
        }
#endif
        /* Apply error to data */
        if (num1 != 0 && loc[j] >= PAD) {
            data[loc[j] - PAD] ^= ALPHA_TO[MODNN(INDEX_OF[num1] + INDEX_OF[num2] + NN - INDEX_OF[den])];
        }
    }

finish:
    if (eras_pos != NULL) {
        for (i = 0; i < count; i++) {
            eras_pos[i] = loc[i];
        }
    }
    retval = count;
    return retval;
}
// NOLINTEND

// C++ wrapper code
Reed_Solomon_Decoder::Reed_Solomon_Decoder(
    const int symbol_size, const int galois_field_polynomial,
    const int fcr, const int primer, const int nb_roots, const int pad)
{
    m_rs = init_rs_char(
        symbol_size, galois_field_polynomial,
        fcr, primer, nb_roots, pad);
}

Reed_Solomon_Decoder::~Reed_Solomon_Decoder() {
    free_rs_char(m_rs);
}

int Reed_Solomon_Decoder::Decode(uint8_t* data, int* eras_pos, int no_eras) {
    return decode_rs_char(m_rs, data, eras_pos, no_eras);
}