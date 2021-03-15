package com.google.android.apps.internal.games.memoryadvice;

class NodeDivide extends NodeDouble {
  private final NodeDouble left;
  private final NodeDouble right;

  NodeDivide(NodeDouble left, NodeDouble right) {
    this.left = left;
    this.right = right;
  }

  @Override
  Double evaluate(Lookup lookup) throws LookupException {
    return left.evaluate(lookup) / right.evaluate(lookup);
  }
}
