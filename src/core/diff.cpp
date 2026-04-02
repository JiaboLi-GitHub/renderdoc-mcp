#include "core/diff.h"
#include "core/diff_session.h"
#include "core/errors.h"
#include <algorithm>
#include <cctype>
#include <unordered_map>

namespace renderdoc::core {

// ---------------------------------------------------------------------------
// lcsAlign: Standard O(n*m) LCS DP with backtracking.
// Returns a list of (optA, optB) pairs where:
//   (some(i), some(j)) = matched
//   (some(i), none)    = deleted (only in A)
//   (none,    some(j)) = added   (only in B)
// ---------------------------------------------------------------------------
std::vector<AlignedPair> lcsAlign(const std::vector<std::string>& keysA,
                                   const std::vector<std::string>& keysB)
{
    const size_t n = keysA.size();
    const size_t m = keysB.size();

    // Handle empty sequences as edge cases
    if (n == 0 && m == 0) {
        return {};
    }
    if (n == 0) {
        std::vector<AlignedPair> result;
        result.reserve(m);
        for (size_t j = 0; j < m; ++j)
            result.emplace_back(std::nullopt, j);
        return result;
    }
    if (m == 0) {
        std::vector<AlignedPair> result;
        result.reserve(n);
        for (size_t i = 0; i < n; ++i)
            result.emplace_back(i, std::nullopt);
        return result;
    }

    // Build DP table: dp[i][j] = LCS length of keysA[0..i-1] vs keysB[0..j-1]
    // Use (n+1) x (m+1) table
    std::vector<std::vector<int>> dp(n + 1, std::vector<int>(m + 1, 0));
    for (size_t i = 1; i <= n; ++i) {
        for (size_t j = 1; j <= m; ++j) {
            if (keysA[i - 1] == keysB[j - 1]) {
                dp[i][j] = dp[i - 1][j - 1] + 1;
            } else {
                dp[i][j] = std::max(dp[i - 1][j], dp[i][j - 1]);
            }
        }
    }

    // Backtrack to build alignment
    std::vector<AlignedPair> result;
    size_t i = n, j = m;
    while (i > 0 || j > 0) {
        if (i > 0 && j > 0 && keysA[i - 1] == keysB[j - 1]) {
            result.emplace_back(i - 1, j - 1);
            --i; --j;
        } else if (j > 0 && (i == 0 || dp[i][j - 1] >= dp[i - 1][j])) {
            result.emplace_back(std::nullopt, j - 1);
            --j;
        } else {
            result.emplace_back(i - 1, std::nullopt);
            --i;
        }
    }

    std::reverse(result.begin(), result.end());
    return result;
}

// ---------------------------------------------------------------------------
// makeDrawMatchKey
// ---------------------------------------------------------------------------
std::string makeDrawMatchKey(const DrawRecord& rec, bool hasMarkers)
{
    if (hasMarkers) {
        return rec.markerPath + "|" + rec.drawType;
    } else {
        return rec.drawType + "|" + rec.shaderHash + "|" + rec.topology;
    }
}

// ---------------------------------------------------------------------------
// Stub implementations — not yet implemented
// ---------------------------------------------------------------------------
SummaryDiffResult diffSummary(DiffSession& /*session*/)
{
    throw CoreError(CoreError::Code::InternalError, "not yet implemented");
}

DrawsDiffResult diffDraws(DiffSession& /*session*/)
{
    throw CoreError(CoreError::Code::InternalError, "not yet implemented");
}

ResourcesDiffResult diffResources(DiffSession& /*session*/)
{
    throw CoreError(CoreError::Code::InternalError, "not yet implemented");
}

StatsDiffResult diffStats(DiffSession& /*session*/)
{
    throw CoreError(CoreError::Code::InternalError, "not yet implemented");
}

PipelineDiffResult diffPipeline(DiffSession& /*session*/, const std::string& /*markerPath*/)
{
    throw CoreError(CoreError::Code::InternalError, "not yet implemented");
}

ImageCompareResult diffFramebuffer(DiffSession& /*session*/,
                                    uint32_t /*eidA*/, uint32_t /*eidB*/,
                                    int /*target*/,
                                    double /*threshold*/,
                                    const std::string& /*diffOutput*/)
{
    throw CoreError(CoreError::Code::InternalError, "not yet implemented");
}

} // namespace renderdoc::core
