/*
 * Copyright (C) 2020 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License
 */

package View.Experimental;

import Controller.Experimental.ExperimentalTabController;
import View.TabLayout;
import com.intellij.icons.AllIcons;
import com.intellij.openapi.actionSystem.AnActionEvent;
import com.intellij.ui.AnActionButton;
import com.intellij.ui.ToolbarDecorator;
import com.intellij.ui.treeStructure.Tree;
import java.awt.Dimension;
import java.util.ArrayList;
import javax.swing.BorderFactory;
import javax.swing.JLabel;
import javax.swing.JPanel;
import javax.swing.JTree;
import javax.swing.tree.DefaultMutableTreeNode;
import javax.swing.tree.DefaultTreeModel;
import javax.swing.tree.DefaultTreeSelectionModel;
import javax.swing.tree.TreePath;
import javax.swing.tree.TreeSelectionModel;
import org.jdesktop.swingx.VerticalLayout;
import org.jetbrains.annotations.NotNull;

public class ExperimentalTab extends TabLayout {

  private final JLabel jLabel = new JLabel("Experimental Features");
  private JTree jTree;
  private final ExperimentalTabController controller;
  private JPanel decoratorPanel;
  private Dimension treePanelDimension = new Dimension(300, 200);

  public ExperimentalTab(ExperimentalTabController controller) {
    this.controller = controller;
    this.setLayout(new VerticalLayout());
    initComponents();
    addComponents();
  }

  private void initComponents() {
    jTree = new Tree(controller.getQualityAsTree());
    jTree.setRootVisible(false);
    jTree.setSelectionModel(new NonLeafSelection());
    jTree.getSelectionModel().setSelectionMode(TreeSelectionModel.SINGLE_TREE_SELECTION);
    jLabel.setFont(getMainFont());
    decoratorPanel = ToolbarDecorator.createDecorator(jTree)
        .addExtraAction(new ChangeButton())
        .addExtraAction(new RefreshButton())
        .setPreferredSize(treePanelDimension)
        .createPanel();
    decoratorPanel.setBorder(BorderFactory.createTitledBorder("Fidelity Changer"));
  }

  private void addComponents() {
    this.add(jLabel);
    this.add(decoratorPanel);
  }

  private final class RefreshButton extends AnActionButton {

    public RefreshButton() {
      super("Refresh", AllIcons.Actions.Refresh);
    }

    @Override
    public void actionPerformed(@NotNull AnActionEvent anActionEvent) {
      ((DefaultTreeModel) jTree.getModel()).setRoot(controller.getQualityAsTree());
    }
  }

  private final class ChangeButton extends AnActionButton {

    public ChangeButton() {
      super("Set Fidelity", AllIcons.Actions.SetDefault);
    }

    @Override
    public void actionPerformed(@NotNull AnActionEvent anActionEvent) {
      DefaultMutableTreeNode treeNode = (DefaultMutableTreeNode) jTree.getSelectionPath()
          .getLastPathComponent();
      int qualityIndex = treeNode.getParent().getIndex(treeNode);
      controller.setCurrentByteString(
          controller.getQualityAsByteString(qualityIndex));
      ((DefaultTreeModel) jTree.getModel()).setRoot(controller.getQualityAsTree());
    }

    @Override
    public void updateButton(@NotNull AnActionEvent e) {
      e.getPresentation().setEnabled(jTree.getSelectionPath() != null);
    }
  }

  private static class NonLeafSelection extends DefaultTreeSelectionModel {

    private TreePath[] getLeafs(TreePath[] fullPaths) {
      ArrayList<TreePath> paths = new ArrayList<>();

      for (TreePath fullPath : fullPaths) {
        if (!((DefaultMutableTreeNode) fullPath.getLastPathComponent()).isLeaf()) {
          paths.add(fullPath);
        }
      }

      return paths.toArray(fullPaths);
    }

    @Override
    public void setSelectionPaths(TreePath[] treePaths) {
      super.setSelectionPaths(getLeafs(treePaths));
    }

    @Override
    public void addSelectionPaths(TreePath[] treePaths) {
      super.addSelectionPaths(getLeafs(treePaths));
    }
  }
}
