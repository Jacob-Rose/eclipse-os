#include "strip_projection.h"

#include <algorithm>
#include <cmath>

using namespace eio;

std::vector<std::shared_ptr<HSVStripNode>> HSVStripNodeFactory::GenerateAxisRow(HSVStripSegment* stripSegment, int startIdx, int Length, const Coordinate& posStart, const Coordinate& posDeltaPerIdx) {
    float xAmt = 0.f, yAmt = 0.f;

    std::vector<std::shared_ptr<HSVStripNode>> Nodes;
    
    for(int idx = 0; idx < Length; ++idx)
    {
        std::shared_ptr<HSVStripNode_Mapped2D> Node = std::make_shared<HSVStripNode_Mapped2D>(stripSegment, idx + startIdx);
        Node->coord.x = posStart.x + (posDeltaPerIdx.x * idx);
        Node->coord.y = posStart.y + (posDeltaPerIdx.y * idx);
        Nodes.push_back(Node);
    }

    stripSegment->addNodes(Nodes);

    return Nodes;
}

float HSVStripNodeFactory::MedianSpacing(const float (*coords)[2], int count)
{
    if (coords == nullptr || count < 2)
    {
        return 0.0f;
    }

    std::vector<float> gaps;
    gaps.reserve(count - 1);
    for (int idx = 1; idx < count; ++idx)
    {
        gaps.push_back(std::hypot(coords[idx][0] - coords[idx - 1][0], coords[idx][1] - coords[idx - 1][1]));
    }

    // the median, not the mean: a strip that jumps between rows has a few
    // long gaps that would otherwise shrink the whole map
    const auto middle = gaps.begin() + gaps.size() / 2;
    std::nth_element(gaps.begin(), middle, gaps.end());
    return *middle;
}

std::vector<std::shared_ptr<HSVStripNode>> HSVStripNodeFactory::GenerateFromTable(HSVStripSegment* stripSegment, int startIdx, const float (*coords)[2], int count, float pixelSpacing)
{
    std::vector<std::shared_ptr<HSVStripNode>> Nodes;
    if (coords == nullptr || count <= 0)
    {
        return Nodes;
    }

    float scale = 1.0f;
    const float median = pixelSpacing > 0.0f ? MedianSpacing(coords, count) : 0.0f;
    if (median > 1e-6f)
    {
        scale = pixelSpacing / median;
    }

    Nodes.reserve(count);
    for (int idx = 0; idx < count; ++idx)
    {
        std::shared_ptr<HSVStripNode_Mapped2D> Node = std::make_shared<HSVStripNode_Mapped2D>(stripSegment, idx + startIdx);
        Node->coord.x = coords[idx][0] * scale;
        Node->coord.y = coords[idx][1] * scale;
        Nodes.push_back(Node);
    }

    stripSegment->addNodes(Nodes);

    return Nodes;
}
