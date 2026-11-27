#pragma once

#include <QMainWindow>
#include <QPushButton>
#include <QLabel>
#include <QComboBox>
#include <QSpinBox>
#include <QLineEdit>
#include <QProgressBar>
#include <memory>
#include "ConfigManager.h"

/**
 * @class MainWindow
 * @brief Main application window
 */
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();
    
    /**
     * @brief Get configuration
     * @return Current configuration
     */
    ApplicationConfig getConfiguration() const;

private slots:
    void onStartCall();
    void onStopCall();
    void onSettingsClicked();
    void onPresetChanged(int index);
    void onStatusUpdate(const QString& status);
    void onStatisticsUpdate(double loss, double rtt, double jitter);

private:
    void createUI();
    void connectSignals();
    void loadConfiguration();
    void saveConfiguration();
    void updateStatusDisplay();
    
    // UI Components
    QWidget *centralWidget_;
    
    // Connection settings
    QLineEdit *localIpEdit_;
    QSpinBox *localPortSpinBox_;
    QLineEdit *remoteIpEdit_;
    QSpinBox *remotePortSpinBox_;
    
    // Quality preset selector
    QComboBox *presetComboBox_;
    QLabel *presetDescriptionLabel_;
    
    // Call controls
    QPushButton *startButton_;
    QPushButton *stopButton_;
    QPushButton *settingsButton_;
    
    // Status display
    QLabel *statusLabel_;
    QLabel *connectionStatusLabel_;
    QProgressBar *signalQualityBar_;
    
    // Statistics
    QLabel *lossRateLabel_;
    QLabel *rttLabel_;
    QLabel *jitterLabel_;
    QLabel *bandwidthLabel_;
    
    // Configuration
    std::unique_ptr<ConfigManager> config_manager_;
    bool is_connected_ = false;
};

