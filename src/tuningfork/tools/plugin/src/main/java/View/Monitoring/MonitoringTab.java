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
package View.Monitoring;

import Controller.Monitoring.MonitoringController;
import com.google.android.performanceparameters.v1.PerformanceParameters.DeviceSpec;
import com.google.android.performanceparameters.v1.PerformanceParameters.UploadTelemetryRequest;
import Utils.Monitoring.RequestServer;
import View.TabLayout;
import com.intellij.openapi.ui.ComboBox;
import java.awt.Dimension;
import java.awt.Font;
import java.awt.GridLayout;
import java.awt.Toolkit;
import java.beans.PropertyChangeEvent;
import java.beans.PropertyChangeListener;
import java.io.IOException;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Set;
import java.util.Vector;
import java.util.function.Consumer;
import javax.swing.Box;
import javax.swing.DefaultComboBoxModel;
import javax.swing.JButton;
import javax.swing.JComboBox;
import javax.swing.JLabel;
import javax.swing.JPanel;
import javax.swing.SwingUtilities;
import org.jdesktop.swingx.VerticalLayout;
import org.jfree.chart.ChartPanel;

public class MonitoringTab extends TabLayout implements PropertyChangeListener {

  private final JLabel title = new JLabel("Telemetry reports");
  private final JLabel nameInfo = new JLabel("APK name: ");
  private final JLabel brand = new JLabel("Brand: ");
  private final JLabel cpuFreqs = new JLabel("CPU core frequencies(Hz): ");
  private final JLabel device = new JLabel("Device: ");
  private final JLabel totalMem = new JLabel("Total memory bytes: ");
  private final JLabel warningLabel = new JLabel(
      "<html> This feature requires running the app on a local"
          + "endpoint in the background.<br> Endpoint uri must be set to \"http://10.0.2.2:9000\"."
          + "</html>");
  private final JButton startMonitoring = new JButton("Start monitoring");
  private final JButton stopMonitoring = new JButton("Stop monitoring");

  private static final Font MAIN_FONT = new Font(Font.SANS_SERIF, Font.BOLD, 18);
  private static final Font MIDDLE_FONT = new Font(Font.SANS_SERIF, Font.BOLD, 12);
  private static final Font SMALL_FONT = new Font(Font.SANS_SERIF, Font.PLAIN, 12);
  private static final Dimension SCREEN_SIZE = Toolkit.getDefaultToolkit().getScreenSize();
  private final Dimension chartSize = new Dimension(SCREEN_SIZE.width / 4,
      SCREEN_SIZE.height / 4);

  private JPanel retrievedInformationPanel;
  private JPanel loadingPanel;
  private JPanel graphPanel;
  private JLabel nameData;
  private JLabel brandData;
  private JLabel cpuFreqsData;
  private JLabel deviceData;
  private JLabel totalMemData;
  private JPanel gridPanel;
  private JComboBox<String> instrumentIDComboBox;
  private ArrayList<ChartPanel> histogramsGraphPanels = new ArrayList<>();
  private MonitoringController controller;

  public MonitoringTab() {
    this.setLayout(new VerticalLayout());
    setSize();
    initComponents();
    addComponents();
  }

  private void addComboBoxData(Set<String> renderTimeHistogramsKeys) {
    List<String> uniqueIDs = new ArrayList<>(renderTimeHistogramsKeys);
    Vector<String> uniqueIdVector = new Vector<>();
    uniqueIdVector.addAll(uniqueIDs);
    uniqueIdVector.add(0, "Instrument ID");

    String selectedObject = null;
    if (uniqueIDs.contains(instrumentIDComboBox.getSelectedItem())) {
      selectedObject = (String) instrumentIDComboBox.getSelectedItem();
    }
    instrumentIDComboBox.setModel(new DefaultComboBoxModel<>(uniqueIdVector));
    if (selectedObject != null) {
      instrumentIDComboBox.setSelectedItem(selectedObject);
    }
  }

  private void deleteExistingGraphs() {
    for (ChartPanel panel : histogramsGraphPanels) {
      graphPanel.remove(panel);
    }
    histogramsGraphPanels = new ArrayList<>();
  }

  private void plotData() {
    deleteExistingGraphs();
    controller.createChartPanels();
    graphPanel.revalidate();
  }

  private void changePanelVisibility(int index) {
    for (int i = 0; i < histogramsGraphPanels.size(); i++) {
      if (i == index) {
        histogramsGraphPanels.get(i).setVisible(true);
      } else {
        histogramsGraphPanels.get(i).setVisible(false);
      }
    }
  }

  private void refreshUI() {
    plotData();
    retrievedInformationPanel.setVisible(true);
    loadingPanel.setVisible(false);
    SwingUtilities.updateComponentTreeUI(retrievedInformationPanel);
  }

  public void setMonitoringTabData(UploadTelemetryRequest telemetryRequest) {
    if (!controller.checkFidelityParams(telemetryRequest)) {
      instrumentIDComboBox.setModel(new DefaultComboBoxModel<>());
      deleteExistingGraphs();
    }
    nameData.setText(telemetryRequest.getName());

    DeviceSpec deviceSpec = telemetryRequest.getSessionContext().getDevice();
    totalMemData.setText(Long.toString(deviceSpec.getTotalMemoryBytes()));
    brandData.setText(deviceSpec.getBrand());
    deviceData.setText(deviceSpec.getDevice());
    cpuFreqsData.setText(Arrays.toString(deviceSpec.getCpuCoreFreqsHzList().toArray()));

    controller.setRenderTimeHistograms(telemetryRequest);
    addComboBoxData(controller.getRenderTimeHistogramsKeys());

    startMonitoring.setVisible(false);
    stopMonitoring.setVisible(true);
    refreshUI();
  }

  private void setNoData() {
    nameData.setText("N/A");
    totalMemData.setText("N/A");
    brandData.setText("N/A");
    deviceData.setText("N/A");
    cpuFreqsData.setText("N/A");
  }

  private void addComponents() {
    this.add(title);
    this.add(Box.createVerticalStrut(10));
    this.add(warningLabel);
    this.add(Box.createVerticalStrut(10));
    JPanel buttonPanel1 = new JPanel();
    buttonPanel1.add(startMonitoring);
    this.add(buttonPanel1);
    JPanel buttonPanel2 = new JPanel();
    buttonPanel2.add(stopMonitoring);
    this.add(buttonPanel2);
    this.add(loadingPanel);

    gridPanel.add(nameInfo);
    gridPanel.add(nameData);
    gridPanel.add(brand);
    gridPanel.add(brandData);
    gridPanel.add(cpuFreqs);
    gridPanel.add(cpuFreqsData);
    gridPanel.add(device);
    gridPanel.add(deviceData);
    gridPanel.add(totalMem);
    gridPanel.add(totalMemData);

    retrievedInformationPanel.add(gridPanel);
    retrievedInformationPanel.add(instrumentIDComboBox);
    retrievedInformationPanel.add(graphPanel);

    this.add(retrievedInformationPanel);
  }

  private void initComponents() {
    controller = new MonitoringController();
    controller.addPropertyChangeListener(this);

    title.setFont(MAIN_FONT);
    nameInfo.setFont(MIDDLE_FONT);
    brand.setFont(MIDDLE_FONT);
    cpuFreqs.setFont(MIDDLE_FONT);
    device.setFont(MIDDLE_FONT);
    totalMem.setFont(MIDDLE_FONT);
    warningLabel.setFont(new Font(Font.SANS_SERIF, Font.ITALIC, 12));

    nameData = new JLabel();
    brandData = new JLabel();
    cpuFreqsData = new JLabel();
    deviceData = new JLabel();
    totalMemData = new JLabel();
    setNoData();

    nameData.setFont(SMALL_FONT);
    brandData.setFont(SMALL_FONT);
    cpuFreqsData.setFont(SMALL_FONT);
    deviceData.setFont(SMALL_FONT);
    totalMemData.setFont(SMALL_FONT);

    instrumentIDComboBox = new ComboBox<>();
    DefaultComboBoxModel<String> comboBoxModel = new DefaultComboBoxModel<>();
    comboBoxModel.addElement("Instrument ID");
    instrumentIDComboBox.setModel(comboBoxModel);
    instrumentIDComboBox.addActionListener(actionEvent -> {
      if (instrumentIDComboBox.getSelectedIndex() == 0) {
        return;
      }
      changePanelVisibility(instrumentIDComboBox.getSelectedIndex() - 1);
    });

    gridPanel = new JPanel(new GridLayout(5, 2));
    Consumer<UploadTelemetryRequest> requestConsumer = this::setMonitoringTabData;
    startMonitoring.addActionListener(actionEvent -> {
      try {
        RequestServer.listen(requestConsumer);
        startMonitoring.setVisible(false);
        stopMonitoring.setVisible(true);
      } catch (IOException e) {
        e.printStackTrace();
      }
    });

    stopMonitoring.addActionListener(actionEvent -> {
      retrievedInformationPanel.setVisible(false);
      try {
        RequestServer.stopListening();
      } catch (IOException e) {
        e.printStackTrace();
      }
      setNoData();
      startMonitoring.setVisible(true);
      stopMonitoring.setVisible(false);
    });

    stopMonitoring.setVisible(false);

    loadingPanel = new JPanel();
    // TODO(@targintaru) add loading gif
    retrievedInformationPanel = new JPanel(new VerticalLayout());
    retrievedInformationPanel.setVisible(false);
    graphPanel = new JPanel(new VerticalLayout());
    graphPanel.setMaximumSize(new Dimension(500, 200));
    graphPanel.revalidate();
  }

  @Override
  public void propertyChange(PropertyChangeEvent propertyChangeEvent) {
    if (propertyChangeEvent.getPropertyName().equals("addChart")) {
      ChartPanel chartPanel = (ChartPanel) propertyChangeEvent.getNewValue();
      chartPanel.setMaximumSize(chartSize);
      chartPanel.setMinimumSize(chartSize);
      chartPanel.setPreferredSize(chartSize);

      String instrumentID = (String) propertyChangeEvent.getOldValue();
      if (instrumentIDComboBox.getSelectedItem().equals(instrumentID)) {
        chartPanel.setVisible(true);
      } else {
        chartPanel.setVisible(false);
      }

      graphPanel.add(chartPanel);
      histogramsGraphPanels.add(chartPanel);
    }
  }
}
