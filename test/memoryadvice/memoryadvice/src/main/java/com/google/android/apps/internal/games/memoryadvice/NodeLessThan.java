package com.google.android.apps.internal.games.memoryadvice;

class NodeLessThan extends NodeBoolean {
  private final NodeDouble left;
  private final NodeDouble right;

  NodeLessThan(NodeDouble left, NodeDouble right) {
    this.left = left;
    this.right = right;
  }

  @Override
  boolean evaluate(Lookup lookup) throws LookupException {
    return left.evaluate(lookup) < right.evaluate(lookup);
  }
}
