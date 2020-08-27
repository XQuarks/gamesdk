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

package Controller.Fidelity;

import Controller.Enum.EnumController;
import Model.MessageDataModel;
import Model.MessageDataModel.Type;
import View.Fidelity.FidelityTableData;
import View.Fidelity.FieldType;
import java.util.List;
import javax.swing.JTable;

public class FidelityTabController extends EnumController {

  private MessageDataModel fidelityDataModel;

  public FidelityTabController(){}

  @Override
  public void onEnumTableChanged() {

  }

  public MessageDataModel getFidelityData() {
    return fidelityDataModel;
  }

  public void addRowAction(JTable jtable) {
    FidelityTableModel model = (FidelityTableModel) jtable.getModel();
    model.addRow(new FidelityTableData(FieldType.INT32, "", ""));
  }

  public void removeRowAction(JTable jtable) {
    FidelityTableModel model = (FidelityTableModel) jtable.getModel();
    int row = jtable.getSelectedRow();
    if (jtable.getCellEditor() != null) {
      jtable.getCellEditor().stopCellEditing();
    }
    model.removeRow(row);
  }

  public boolean saveSettings(JTable jTable) {
    List<String> fidelityParamNames = ((FidelityTableModel) jTable.getModel())
        .getFidelityParamNames();
    List<String> fidelityFieldValues = ((FidelityTableModel) jTable.getModel())
        .getFidelityFieldValues();
    fidelityDataModel = new MessageDataModel();
    fidelityDataModel.setMessageType(Type.FIDELITY);
    fidelityDataModel.addMultipleFields(fidelityParamNames, fidelityFieldValues);
    return true;
  }
}
