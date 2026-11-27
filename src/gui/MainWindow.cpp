#include "MainWindow.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QLabel>
#include <QMessageBox>
#include <QJsonDocument>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), config_manager_(std::make_unique<ConfigManager>()) {
    
    setWindowTitle("AudioChat - P2P Audio Communication");
    setGeometry(100, 100, 600, 500);
    
    config_manager_->loadPreset(QualityPreset::Balanced);
    createUI();
    connectSignals();
    loadConfiguration();
}

MainWindow::~MainWindow() = default;

ApplicationConfig MainWindow::getConfiguration() const {
    ApplicationConfig config = config_manager_->getConfig();
    
    config.network.local_ip = localIpEdit_->text().toStdString();
    config.network.local_port = localPortSpinBox_->value();
    config.network.remote_ip = remoteIpEdit_->text().toStdString();
    config.network.remote_port = remotePortSpinBox_->value();
    
    return config;
}

void MainWindow::createUI() {
    centralWidget_ = new QWidget(this);
    QVBoxLayout *mainLayout = new QVBoxLayout(centralWidget_);
    
    // Connection Settings Group
    QGroupBox *connectionGroup = new QGroupBox("Connection Settings", this);
    QVBoxLayout *connectionLayout = new QVBoxLayout(connectionGroup);
    
    QHBoxLayout *localLayout = new QHBoxLayout();
    localLayout->addWidget(new QLabel("Local IP:"));
    localIpEdit_ = new QLineEdit("127.0.0.1");
    localLayout->addWidget(localIpEdit_);
    localLayout->addWidget(new QLabel("Port:"));
    localPortSpinBox_ = new QSpinBox();
    localPortSpinBox_->setValue(10088);
    localPortSpinBox_->setMaximum(65535);
    localLayout->addWidget(localPortSpinBox_);
    connectionLayout->addLayout(localLayout);
    
    QHBoxLayout *remoteLayout = new QHBoxLayout();
    remoteLayout->addWidget(new QLabel("Remote IP:"));
    remoteIpEdit_ = new QLineEdit("127.0.0.1");
    remoteLayout->addWidget(remoteIpEdit_);
    remoteLayout->addWidget(new QLabel("Port:"));
    remotePortSpinBox_ = new QSpinBox();
    remotePortSpinBox_->setValue(10086);
    remotePortSpinBox_->setMaximum(65535);
    remoteLayout->addWidget(remotePortSpinBox_);
    connectionLayout->addLayout(remoteLayout);
    
    mainLayout->addWidget(connectionGroup);
    
    // Quality Preset Group
    QGroupBox *presetGroup = new QGroupBox("Quality Preset", this);
    QVBoxLayout *presetLayout = new QVBoxLayout(presetGroup);
    
    QHBoxLayout *presetSelectLayout = new QHBoxLayout();
    presetSelectLayout->addWidget(new QLabel("Preset:"));
    presetComboBox_ = new QComboBox();
    presetComboBox_->addItem("Low Latency (10ms, 24kbps)");
    presetComboBox_->addItem("Balanced (20ms, 32kbps)");
    presetComboBox_->addItem("High Quality (40ms, 64kbps)");
    presetComboBox_->setCurrentIndex(1);
    presetSelectLayout->addWidget(presetComboBox_);
    presetLayout->addLayout(presetSelectLayout);
    
    presetDescriptionLabel_ = new QLabel(
        "Balanced: Default configuration for good quality and latency");
    presetDescriptionLabel_->setWordWrap(true);
    presetLayout->addWidget(presetDescriptionLabel_);
    
    mainLayout->addWidget(presetGroup);
    
    // Call Controls Group
    QGroupBox *controlsGroup = new QGroupBox("Call Controls", this);
    QHBoxLayout *controlsLayout = new QHBoxLayout(controlsGroup);
    
    startButton_ = new QPushButton("Start Call");
    stopButton_ = new QPushButton("Stop Call");
    stopButton_->setEnabled(false);
    settingsButton_ = new QPushButton("Settings");
    
    controlsLayout->addWidget(startButton_);
    controlsLayout->addWidget(stopButton_);
    controlsLayout->addWidget(settingsButton_);
    
    mainLayout->addWidget(controlsGroup);
    
    // Status Display Group
    QGroupBox *statusGroup = new QGroupBox("Status", this);
    QVBoxLayout *statusLayout = new QVBoxLayout(statusGroup);
    
    statusLabel_ = new QLabel("Status: Idle");
    connectionStatusLabel_ = new QLabel("Connection: Disconnected");
    
    QLabel *signalQualityLbl = new QLabel("Signal Quality:");
    signalQualityBar_ = new QProgressBar();
    signalQualityBar_->setMaximum(100);
    signalQualityBar_->setValue(0);
    
    statusLayout->addWidget(statusLabel_);
    statusLayout->addWidget(connectionStatusLabel_);
    statusLayout->addWidget(signalQualityLbl);
    statusLayout->addWidget(signalQualityBar_);
    
    mainLayout->addWidget(statusGroup);
    
    // Statistics Group
    QGroupBox *statsGroup = new QGroupBox("Network Statistics", this);
    QVBoxLayout *statsLayout = new QVBoxLayout(statsGroup);
    
    lossRateLabel_ = new QLabel("Packet Loss: 0.0%");
    rttLabel_ = new QLabel("RTT: 0.0 ms");
    jitterLabel_ = new QLabel("Jitter: 0.0 ms");
    bandwidthLabel_ = new QLabel("Bandwidth: 0.0 kbps");
    
    statsLayout->addWidget(lossRateLabel_);
    statsLayout->addWidget(rttLabel_);
    statsLayout->addWidget(jitterLabel_);
    statsLayout->addWidget(bandwidthLabel_);
    
    mainLayout->addWidget(statsGroup);
    
    mainLayout->addStretch();
    setCentralWidget(centralWidget_);
}

void MainWindow::connectSignals() {
    connect(startButton_, &QPushButton::clicked, this, &MainWindow::onStartCall);
    connect(stopButton_, &QPushButton::clicked, this, &MainWindow::onStopCall);
    connect(settingsButton_, &QPushButton::clicked, this, &MainWindow::onSettingsClicked);
    connect(presetComboBox_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onPresetChanged);
}

void MainWindow::loadConfiguration() {
    // Load from file if exists
    std::string config_file = "config/default.yaml";
    config_manager_->loadFromFile(config_file);
    
    auto config = config_manager_->getConfig();
    localIpEdit_->setText(QString::fromStdString(config.network.local_ip));
    localPortSpinBox_->setValue(config.network.local_port);
    remoteIpEdit_->setText(QString::fromStdString(config.network.remote_ip));
    remotePortSpinBox_->setValue(config.network.remote_port);
}

void MainWindow::saveConfiguration() {
    auto config = getConfiguration();
    config_manager_->setNetworkConfig(config.network);
    config_manager_->saveToFile("config/current.yaml");
}

void MainWindow::updateStatusDisplay() {
    QString status = is_connected_ ? "Connected" : "Disconnected";
    connectionStatusLabel_->setText("Connection: " + status);
}

void MainWindow::onStartCall() {
    is_connected_ = true;
    startButton_->setEnabled(false);
    stopButton_->setEnabled(true);
    statusLabel_->setText("Status: Calling...");
    updateStatusDisplay();
    saveConfiguration();
}

void MainWindow::onStopCall() {
    is_connected_ = false;
    startButton_->setEnabled(true);
    stopButton_->setEnabled(false);
    statusLabel_->setText("Status: Idle");
    updateStatusDisplay();
}

void MainWindow::onSettingsClicked() {
    QMessageBox::information(this, "Settings",
        "Settings dialog would open here.\n"
        "Audio device selection, AEC parameters, etc.");
}

void MainWindow::onPresetChanged(int index) {
    const QString descriptions[] = {
        "Low Latency: Minimal delay (10ms), good for real-time applications",
        "Balanced: Default configuration for good quality and latency (20ms)",
        "High Quality: Maximum audio quality (40ms), slightly higher latency"
    };
    
    presetDescriptionLabel_->setText(descriptions[index]);
    
    QualityPreset presets[] = {
        QualityPreset::LowLatency,
        QualityPreset::Balanced,
        QualityPreset::HighQuality
    };
    
    config_manager_->loadPreset(presets[index]);
}

void MainWindow::onStatusUpdate(const QString& status) {
    statusLabel_->setText("Status: " + status);
}

void MainWindow::onStatisticsUpdate(double loss, double rtt, double jitter) {
    lossRateLabel_->setText(QString("Packet Loss: %1%").arg(loss, 0, 'f', 1));
    rttLabel_->setText(QString("RTT: %1 ms").arg(rtt, 0, 'f', 1));
    jitterLabel_->setText(QString("Jitter: %1 ms").arg(jitter, 0, 'f', 1));
    
    // Update signal quality bar
    int quality = 100 - (int)(loss * 10);  // Simple calculation
    quality = std::max(0, std::min(100, quality));
    signalQualityBar_->setValue(quality);
}

