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

package View.Annotation;

import Controller.Annotation.AnnotationTabController;
import Controller.Annotation.AnnotationTableModel;
import Model.EnumDataModel;
import View.EnumTable;
import View.TabLayout;
import com.intellij.ui.ToolbarDecorator;
import com.intellij.ui.components.JBLabel;
import com.intellij.ui.components.JBScrollPane;
import com.intellij.ui.table.JBTable;
import java.util.List;
import javax.swing.Box;
import javax.swing.JPanel;
import org.jdesktop.swingx.VerticalLayout;

public class AnnotationTab extends TabLayout {

  private JBScrollPane scrollPane;
  private JBTable annotationTable;
  private JPanel decoratorPanel;

  private AnnotationTabController annotationController;
  private List<EnumDataModel> enumData;

  private final JBLabel annotationLabel = new JBLabel("Annotation Settings");
  private final JBLabel informationLabel =
      new JBLabel("Annotation is used by tuning fork to mark the histograms being sent.");

  public AnnotationTab(AnnotationTabController annotationController, List<EnumDataModel> enumData) {
    this.annotationController = annotationController;
    this.enumData = enumData;
    initVariables();
    initComponents();
  }

  public AnnotationTabController getAnnotationController() {
    return annotationController;
  }

  private void setColumnsEditorsAndRenderers() {
    List<String> annotationNames = annotationController.getEnumsNames();
    initComboBoxColumns(annotationTable, 0, annotationNames);
    initTextFieldColumns(annotationTable, 1);
  }

  private void initVariables() {
    scrollPane = new JBScrollPane();
    annotationTable = new JBTable();
    decoratorPanel =
        ToolbarDecorator.createDecorator(annotationTable)
            .setAddAction(it -> AnnotationTabController.addRowAction(annotationTable))
            .setRemoveAction(it -> AnnotationTabController.removeRowAction(annotationTable))
            .createPanel();
  }

  private void initComponents() {
    // Initialize layout.
    this.setLayout(new VerticalLayout());
    setSize();

    // Add labels.
    annotationLabel.setFont(getMainFont());
    informationLabel.setFont(getSecondaryLabel());
    this.add(annotationLabel);
    this.add(Box.createVerticalStrut(10));
    this.add(informationLabel);
    // Initialize toolbar and table.
    AnnotationTableModel model = new AnnotationTableModel();
    annotationTable.setModel(model);
    annotationController.addInitialAnnotation(annotationTable);
    setDecoratorPanelSize(decoratorPanel);
    setTableSettings(scrollPane, decoratorPanel, annotationTable);
    setColumnsEditorsAndRenderers();

    this.add(scrollPane);
    annotationController.addEnums(enumData);
    this.add(new EnumTable(annotationController));
  }

  public boolean saveSettings() {
    annotationTable.clearSelection();
    return annotationController.saveSettings(annotationTable);
  }

  public JBTable getAnnotationTable() {
    return annotationTable;
  }
}