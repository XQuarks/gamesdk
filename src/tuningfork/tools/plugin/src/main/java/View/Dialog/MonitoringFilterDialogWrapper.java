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

package View.Dialog;

import Controller.Monitoring.MonitoringController.HistogramTree;
import Controller.Monitoring.MonitoringController.HistogramTree.Node;
import Model.MonitorFilterModel;
import View.Monitoring.MonitoringTab.JTreeNode;
import View.Monitoring.MonitoringTab.TreeToolTipRenderer;
import com.intellij.icons.AllIcons.Actions;
import com.intellij.openapi.actionSystem.AnActionEvent;
import com.intellij.openapi.ui.DialogWrapper;
import com.intellij.ui.AnActionButton;
import com.intellij.ui.ToolbarDecorator;
import java.awt.Dimension;
import java.awt.event.ItemEvent;
import java.beans.PropertyChangeListener;
import java.beans.PropertyChangeSupport;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Optional;
import java.util.Set;
import java.util.Vector;
import java.util.function.Consumer;
import java.util.function.Supplier;
import java.util.stream.Collectors;
import javax.swing.Box;
import javax.swing.JComboBox;
import javax.swing.JComponent;
import javax.swing.JLabel;
import javax.swing.JPanel;
import javax.swing.JTree;
import javax.swing.tree.DefaultMutableTreeNode;
import javax.swing.tree.DefaultTreeModel;
import javax.swing.tree.TreePath;
import org.jdesktop.swingx.VerticalLayout;
import org.jetbrains.annotations.NotNull;
import org.jetbrains.annotations.Nullable;

public class MonitoringFilterDialogWrapper extends DialogWrapper {

  private final HistogramTree histogramTree;
  private final PropertyChangeSupport propertyChangeSupport;
  private FilterPanel filterPanel;

  public MonitoringFilterDialogWrapper(HistogramTree tree) {
    super(true);
    histogramTree = tree;
    propertyChangeSupport = new PropertyChangeSupport(this);
    filterPanel = new FilterPanel();
    setTitle("Histograms Filter");
    init();
  }

  public void addPropertyChangeListener(PropertyChangeListener listener) {
    propertyChangeSupport.addPropertyChangeListener(listener);
  }

  @Nullable
  @Override
  protected JComponent createCenterPanel() {
    return filterPanel;
  }

  @Override
  protected void doOKAction() {
    MonitorFilterModel model = new MonitorFilterModel(filterPanel.getPhoneModel(),
        filterPanel.getAnnotations(), filterPanel.getFidelity());
    propertyChangeSupport.firePropertyChange("addFilter", null, model);
    super.doOKAction();
  }

  private final class FilterPanel extends JPanel {

    private JTree annotationsTree;
    private final List<JTreeNode> selectedAnnotationNodes;
    private JPanel annotationDecorator;
    private JComboBox<Node> phoneModelComboBox, fidelityComboBox;

    public FilterPanel() {
      super(new VerticalLayout());
      selectedAnnotationNodes = new ArrayList<>();
      initComponent();
      addComponents();
    }

    private String getPhoneModel() {
      return ((Node) phoneModelComboBox.getSelectedItem()).getNodeName();
    }

    private String getFidelity() {
      return ((Node) fidelityComboBox.getSelectedItem()).getNodeName();
    }

    private List<String> getAnnotations() {
      return selectedAnnotationNodes.stream().map(JTreeNode::getNodeName)
          .collect(Collectors.toList());
    }

    private void setAnnotations(Node chosenModel) {
      List<Node> annotations = histogramTree.getAllAnnotations(chosenModel);
      JTreeNode rootNode = new JTreeNode();
      for (Node annotationNode : annotations) {
        JTreeNode jTreeNode = new JTreeNode(annotationNode);
        if (selectedAnnotationNodes.contains(jTreeNode)) {
          jTreeNode.setNodeState(true);
        }
        rootNode.add(jTreeNode);
      }
      ((DefaultTreeModel) annotationsTree.getModel()).setRoot(rootNode);
      fidelityComboBox.removeAllItems();
      for (Node node : getAllFidelity()) {
        fidelityComboBox.addItem(node);
      }
    }

    private void addComponents() {
      add(new JLabel("Choose Phone Model to filter against"));
      add(phoneModelComboBox);
      add(new JLabel("Choosing multiple annotation will lead to merging"));
      add(annotationDecorator);
      add(Box.createVerticalStrut(10));
      add(new JLabel("Choose a fidelity to render"));
      add(fidelityComboBox);
      annotationDecorator.setPreferredSize(new Dimension(150, 200));
    }

    private void initComponent() {
      annotationsTree = new JTree(new DefaultMutableTreeNode());
      annotationsTree.setRootVisible(false);
      annotationsTree.setCellRenderer(new TreeToolTipRenderer());
      annotationDecorator = ToolbarDecorator.createDecorator(annotationsTree)
          .addExtraAction(getAnnotationTreeButton()).createPanel();
      phoneModelComboBox = new JComboBox<>(new Vector<>(histogramTree.getAllPhoneModels()));
      fidelityComboBox = new JComboBox<>(new Vector<>(getAllFidelity()));
      Node selectedItem = (Node) phoneModelComboBox.getSelectedItem();
      if (selectedItem != null) {
        setAnnotations(selectedItem);
      }
      phoneModelComboBox.addItemListener(event -> {
        if (event.getStateChange() == ItemEvent.SELECTED) {
          Node chosenNode = (Node) phoneModelComboBox.getSelectedItem();
          setAnnotations(chosenNode);
        }
      });
    }

    private Set<Node> getAllFidelity() {
      List<Node> chosenAnnotationNodes = selectedAnnotationNodes.stream()
          .map(jTreeNode -> histogramTree.findNodeByName(jTreeNode.getNodeName())).collect(
              Collectors.toList());
      return histogramTree.getAllFidelity(chosenAnnotationNodes);
    }

    private SelectAction getAnnotationTreeButton() {
      return new SelectAction(annotationsTree, () -> annotationsTree.getSelectionPath() != null,
          lastNodes -> lastNodes.forEach(lastNode -> {
            Optional<JTreeNode> nodeExist = selectedAnnotationNodes.stream()
                .filter(node -> node.getNodeName().equals(lastNode.getNodeName())).findFirst();
            if (nodeExist.isPresent()) {
              selectedAnnotationNodes.remove(nodeExist.get());
            } else {
              selectedAnnotationNodes.add(lastNode);
            }
            setAnnotations((Node) phoneModelComboBox.getSelectedItem());
          }));
    }

    private class SelectAction extends AnActionButton {

      private final JTree jTree;
      private final Consumer<List<JTreeNode>> consumer;
      private final Supplier<Boolean> supplier;

      public SelectAction(JTree jTree, Supplier<Boolean> update,
          Consumer<List<JTreeNode>> consumer) {
        super("Toggle", Actions.SetDefault);
        this.jTree = jTree;
        this.supplier = update;
        this.consumer = consumer;
      }

      @Override
      public void updateButton(@NotNull AnActionEvent e) {
        e.getPresentation().setEnabled(supplier.get());
      }

      @Override
      public void actionPerformed(@NotNull AnActionEvent anActionEvent) {
        TreePath[] selectedPath = jTree.getSelectionPaths();
        if (selectedPath == null) {
          return;
        }
        List<JTreeNode> lastNodes = Arrays.stream(jTree.getSelectionPaths())
            .map(path -> (JTreeNode) path.getLastPathComponent()).collect(Collectors.toList());
        consumer.accept(lastNodes);
      }
    }

  }
}
