#include "strip_projection.h"

using namespace eio;


vector<shared_ptr<HSVStripNode>> HSVStripNodeFactory::GenerateAxisRow(HSVStrip* strip, int startIdx, int Length, const Coordinate& posStart, const Coordinate& posDelta) {
    float xAmt = 0.f, yAmt = 0.f;

    vector<shared_ptr<HSVStripNode>> Nodes;
    
    for(int idx = 0; idx < Length; ++idx)
    {
        shared_ptr<HSVStripNode_Mapped2D> Node = make_shared<HSVStripNode_Mapped2D>(strip, idx + startIdx);
        Node->coord.x = posStart.x + (posDelta.x * idx);
        Node->coord.y = posStart.y + (posDelta.y * idx);;
        Nodes.push_back(Node);
    }

    return Nodes;
}