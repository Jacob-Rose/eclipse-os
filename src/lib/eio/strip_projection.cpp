#include "strip_projection.h"

using namespace eio;

vector<shared_ptr<HSVStripNode_Mapped2D>> HSVStripNodeFactory::GenerateAxisRow(float xDelta, float yDelta, int startIdx, int Length) {
    float xAmt = 0.f, yAmt = 0.f;

    vector<shared_ptr<HSVStripNode_Mapped2D>> Nodes;
    
    for(int idx = 0; idx < Length; ++idx)
    {
        shared_ptr<HSVStripNode_Mapped2D> Node = make_shared<HSVStripNode_Mapped2D>();
        Node->stripIdx = idx + startIdx;
        Node->coord.x = xAmt;
        Node->coord.y = yAmt;
        Nodes.push_back(Node);
    }

    return Nodes;
}