#include "stealthxconfig.h"
#include "ui_stealthxconfig.h"

#include "bitcoinunits.h"
#include "guiconstants.h"
#include "optionsmodel.h"
#include "walletmodel.h"
#include "init.h"

#include <QMessageBox>
#include <QPushButton>
#include <QKeyEvent>
#include <QSettings>

StealthXConfig::StealthXConfig(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::StealthXConfig),
    model(0)
{
    ui->setupUi(this);

    connect(ui->buttonBasic, SIGNAL(clicked()), this, SLOT(clickBasic()));
    connect(ui->buttonHigh, SIGNAL(clicked()), this, SLOT(clickHigh()));
    connect(ui->buttonMax, SIGNAL(clicked()), this, SLOT(clickMax()));
}

StealthXConfig::~StealthXConfig()
{
    delete ui;
}

void StealthXConfig::setModel(WalletModel *model)
{
    this->model = model;
}

void StealthXConfig::clickBasic()
{
    configure(true, 1000, 2);

    QString strAmount(BitcoinUnits::formatWithUnit(
        model->getOptionsModel()->getDisplayUnit(), 1000 * COIN));
    QMessageBox::information(this, tr("StealthX Configuration"),
        tr(
            "StealthX was successfully set to basic (%1 and 2 rounds). You can change this at any time by opening Redux's configuration screen."
        ).arg(strAmount)
    );

    close();
}

void StealthXConfig::clickHigh()
{
    configure(true, 1000, 8);

    QString strAmount(BitcoinUnits::formatWithUnit(
        model->getOptionsModel()->getDisplayUnit(), 1000 * COIN));
    QMessageBox::information(this, tr("StealthX Configuration"),
        tr(
            "StealthX was successfully set to high (%1 and 8 rounds). You can change this at any time by opening Redux's configuration screen."
        ).arg(strAmount)
    );

    close();
}

void StealthXConfig::clickMax()
{
    configure(true, 1000, 16);

    QString strAmount(BitcoinUnits::formatWithUnit(
        model->getOptionsModel()->getDisplayUnit(), 1000 * COIN));
    QMessageBox::information(this, tr("StealthX Configuration"),
        tr(
            "StealthX was successfully set to maximum (%1 and 16 rounds). You can change this at any time by opening Redux's configuration screen."
        ).arg(strAmount)
    );

    close();
}

void StealthXConfig::configure(bool enabled, int coins, int rounds) {

    QSettings settings;

    settings.setValue("nStealthXRounds", rounds);
    settings.setValue("nAnonymizeReduxcoinAmount", coins);

    nStealthXRounds = rounds;
    nAnonymizeReduxcoinAmount = coins;
}
