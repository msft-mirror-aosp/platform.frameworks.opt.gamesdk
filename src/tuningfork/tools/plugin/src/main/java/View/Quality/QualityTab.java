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

package View.Quality;

import Controller.Quality.QualityTabController;
import Controller.Quality.QualityTableModel;
import View.Decorator.TableRenderer.RoundedCornerRenderer;
import View.Quality.QualityDecorators.EnumOptionsDecorator;
import View.Quality.QualityDecorators.HeaderCenterLabel;
import View.Quality.QualityDecorators.ParameterNameRenderer;
import View.Quality.QualityDecorators.TrendRenderer;
import View.TabLayout;
import com.intellij.ui.ToolbarDecorator;
import com.intellij.ui.table.JBTable;
import java.awt.Dimension;
import java.beans.PropertyChangeEvent;
import java.beans.PropertyChangeListener;
import javax.swing.JLabel;
import javax.swing.JPanel;
import javax.swing.JScrollPane;
import javax.swing.JTable;
import javax.swing.table.JTableHeader;
import javax.swing.table.TableCellEditor;
import javax.swing.table.TableCellRenderer;
import javax.swing.table.TableColumn;
import org.jdesktop.swingx.VerticalLayout;


public class QualityTab extends TabLayout implements PropertyChangeListener {

  private final static JLabel title = new JLabel("Quality levels");
  private JScrollPane scrollPane;

  private final static JLabel aboutQualitySettings = new JLabel(
      "<html> All quality settings are saved into " +
          "app/src/main/assets/tuningfork/dev_tuningfork_fidelityparams_*.txt files. <br>" +
          "You should have at least one quality level. <br>" +
          "Once you add a new level, you can edit/add data it by" +
          "modifying the text in the table below.</html> ");

  private JBTable qualityParametersTable;
  private QualityTableModel qualityTableModel;
  private QualityTabController qualityTabController;
  private JPanel decoratorPanel;

  public QualityTab(QualityTabController qualityTabController) {
    this.setLayout(new VerticalLayout());
    this.qualityTabController = qualityTabController;
    setSize();
    initComponents();
    addComponents();
  }

  private void addComponents() {
    this.add(title);
    this.add(aboutQualitySettings);
    this.add(decoratorPanel);
  }

  private void initComponents() {
    title.setFont(getMainFont());
    aboutQualitySettings.setFont(getSecondaryLabel());

    qualityTableModel = new QualityTableModel(qualityTabController);
    qualityParametersTable = new JBTable(qualityTableModel) {
      @Override
      public JTableHeader getTableHeader() {
        JTableHeader jTableHeader = super.getTableHeader();
        setColumnsSize(jTableHeader);
        return jTableHeader;
      }

      @Override
      public TableCellRenderer getCellRenderer(int row, int column) {
        if (column == 0) {
          return new ParameterNameRenderer();
        } else if (column == 1) {
          return new TrendRenderer();
        } else {
          if (qualityTabController.isEnum(qualityTableModel.getValueAt(row, 0).toString())) {
            return new EnumOptionsDecorator(qualityTabController
                .getEnumOptionsByName(qualityTableModel.getValueAt(row, 0).toString()));
          } else {
            return new RoundedCornerRenderer();
          }
        }

      }

      @Override
      public TableCellEditor getCellEditor(int row, int column) {
        if (column <= 1) {
          return getTextFieldModel();
        } else {
          if (qualityTabController.isEnum(qualityTableModel.getValueAt(row, 0).toString())) {
            return new EnumOptionsDecorator(qualityTabController
                .getEnumOptionsByName(qualityTableModel.getValueAt(row, 0).toString()));
          } else {
            return getTextFieldModel();
          }
        }

      }
    };
    decoratorPanel = ToolbarDecorator.createDecorator(qualityParametersTable)
        .setAddAction(anActionButton -> qualityTabController.addColumn(qualityParametersTable))
        .setRemoveAction(
            anActionButton -> qualityTabController.removeColumn(qualityParametersTable))
        .setRemoveActionUpdater(e -> qualityParametersTable.getSelectedColumn() > 1)
        .setMinimumSize(new Dimension(600, 500))
        .createPanel();

    setDecoratorPanelSize(decoratorPanel);
    setTableVisuals();
  }

  private void setTableVisuals() {
    qualityParametersTable.getTableHeader().setDefaultRenderer(new HeaderCenterLabel());
    qualityParametersTable.getTableHeader().setReorderingAllowed(false);
    qualityParametersTable.setRowSelectionAllowed(false);
    qualityParametersTable.setCellSelectionEnabled(false);
    qualityParametersTable.setColumnSelectionAllowed(true);
    qualityParametersTable.setSelectionBackground(null);
    qualityParametersTable.setSelectionForeground(null);
    qualityParametersTable.setIntercellSpacing(new Dimension(0, 0));
    qualityParametersTable.setAutoResizeMode(JTable.AUTO_RESIZE_OFF);
    qualityParametersTable.setRowHeight(25);
    setColumnsSize(qualityParametersTable.getTableHeader());
  }

  private void setColumnsSize(JTableHeader jTableHeader) {
    TableColumn parameterNameCol = jTableHeader.getColumnModel().getColumn(0);
    parameterNameCol.setMaxWidth(140);
    parameterNameCol.setMinWidth(140);
    parameterNameCol.setPreferredWidth(140);
    TableColumn trendCol = jTableHeader.getColumnModel().getColumn(1);
    trendCol.setPreferredWidth(80);
    trendCol.setMaxWidth(80);
    trendCol.setMinWidth(80);
    for (int i = 2; i < jTableHeader.getColumnModel().getColumnCount(); i++) {
      TableColumn dataColumn = jTableHeader.getColumnModel().getColumn(i);
      dataColumn.setMinWidth(120);
      dataColumn.setMaxWidth(120);
      dataColumn.setPreferredWidth(120);
    }
  }

  @Override
  public void propertyChange(PropertyChangeEvent evt) {
    if (evt.getPropertyName().equals("addField")) {
      qualityTabController.addRow(qualityParametersTable);
    } else if (evt.getPropertyName().equals("nameChange")) {
      String newName = evt.getNewValue().toString();
      int fieldRow = qualityTabController.getFieldIndexByName(evt.getOldValue().toString());
      qualityTableModel.setValueAt(newName, fieldRow, 0);
    } else if (evt.getPropertyName().equals("removeField")) {
      int row = Integer.parseInt(evt.getNewValue().toString());
      qualityTabController.removeRow(qualityParametersTable, row);
    }
  }
}
