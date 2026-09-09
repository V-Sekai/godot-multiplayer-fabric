#include "EdgeInfo.h"

namespace mwt {

EdgeInfo::EdgeInfo(){
	leftsize = -1; rightsize = -1;
	// Null so ~EdgeInfo is safe for edges the buildList triangle scan
	// never reaches (the arrays are only allocated on first visit).
	leftTris = nullptr; leftEdgeInd = nullptr;
	rightTris = nullptr; rightEdgeInd = nullptr;
}

EdgeInfo::~EdgeInfo(){
	delete [] leftTris;  delete [] leftEdgeInd;
	delete [] rightTris; delete [] rightEdgeInd;
}

int EdgeInfo::getSize(){
	int totalSize = 0;
	totalSize += leftsize*(sizeof(int)+sizeof(char));
	totalSize += rightsize*(sizeof(int)+sizeof(char));
	return totalSize;
}

}  // namespace mwt
