/* Kernels fed to the LLVM shift-cost pass.
 *
 * They are not linked into the timed model. clang -O2 must keep the loops,
 * so the arrays are incoming pointers and vectorization/unrolling is off.
 * N is a constant so ScalarEvolution can see the trip count and the stride.
 */
#define N 64

__attribute__((noinline))
void row_sum(float *restrict out, const float *restrict A) {
    for (int i = 0; i < N; ++i) {
        float s = 0.f;
#pragma clang loop vectorize(disable) unroll(disable)
        for (int j = 0; j < N; ++j)
            s += A[i * N + j];
        out[i] = s;
    }
}

__attribute__((noinline))
void col_sum(float *restrict out, const float *restrict A) {
    for (int j = 0; j < N; ++j) {
        float s = 0.f;
#pragma clang loop vectorize(disable) unroll(disable)
        for (int i = 0; i < N; ++i)
            s += A[i * N + j];
        out[j] = s;
    }
}

__attribute__((noinline))
int gather_sum(int *restrict out, const int *restrict vals, const int *restrict idx) {
    int s = 0;
#pragma clang loop vectorize(disable) unroll(disable)
    for (int i = 0; i < N; ++i) {
        s += vals[idx[i]];
        out[i] = s;
    }
    return s;
}
