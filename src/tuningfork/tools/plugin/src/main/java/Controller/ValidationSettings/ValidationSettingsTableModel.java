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

package Controller.ValidationSettings;

import javax.swing.table.AbstractTableModel;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.stream.Collectors;


public class ValidationSettingsTableModel extends AbstractTableModel {

    private static final String[] COLUMN_NAMES = {
            "instrument_key (int32)",
            "bucket_min (float)",
            "bucket_max (float)",
            "n_buckets (int32)"
    };
    private List<String[]> data;

    public ValidationSettingsTableModel() {
        data = new ArrayList<>();
    }

    @Override
    public int getRowCount() {
        return data.size();
    }

    @Override
    public int getColumnCount() {
        return COLUMN_NAMES.length;
    }

    @Override
    public Object getValueAt(int rowIndex, int columnIndex) {
        return data.get(rowIndex)[columnIndex];
    }

    @Override
    public String getColumnName(int column) {
        return COLUMN_NAMES[column];
    }

    public void addRow(String[] row) {
        data.add(row);
        fireTableRowsInserted(getRowCount() - 1, getRowCount());
    }

    @Override
    public boolean isCellEditable(int row, int column) {
        return true;
    }

    public List<String> getColumnNames() {
        return Arrays.asList(COLUMN_NAMES);
    }

    public List<List<String>> getValidationSettings() {
        List<List<String>> validationSettings = new ArrayList<>();
        for (String[] row : data) {
            validationSettings.add(Arrays.asList(row));
        }
        return validationSettings;
    }

    @Override
    public void setValueAt(Object value, int row, int column) {
        data.get(row)[column] = value.toString();
        fireTableCellUpdated(row, column);
    }

    public void removeRow(int row) {
        data.remove(row);
        fireTableDataChanged();
    }
}