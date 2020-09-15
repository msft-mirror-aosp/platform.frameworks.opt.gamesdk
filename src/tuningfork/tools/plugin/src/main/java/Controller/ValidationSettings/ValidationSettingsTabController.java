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

import javax.swing.JTable;
import java.util.Random;

public class ValidationSettingsTabController {
    public static void addRowAction(JTable jtable) {
        ValidationSettingsTableModel model = (ValidationSettingsTableModel) jtable.getModel();
        // TODO(volobushek): change to actual insert data.
        Random rand = new Random();
        String first = Math.abs(rand.nextInt() % 100) + "";
        String second = rand.nextFloat() * 100 + "";
        String third = rand.nextFloat() * 100 + "";
        String fourth = Math.abs(rand.nextInt() % 100) + "";
        model.addRow(new String[] {first, second, third, fourth});
    }

    public static void removeRowAction(JTable jtable) {
        ValidationSettingsTableModel model = (ValidationSettingsTableModel) jtable.getModel();
        int row = jtable.getSelectedRow();
        if (jtable.getCellEditor() != null) {
            jtable.getCellEditor().stopCellEditing();
        }
        model.removeRow(row);
    }
}