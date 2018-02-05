#ifndef STEALTHXCONFIG_H
#define STEALTHXCONFIG_H

#include <QDialog>

namespace Ui {
    class StealthXConfig;
}
class WalletModel;

/** Multifunctional dialog to ask for passphrases. Used for encryption, unlocking, and changing the passphrase.
 */
class StealthXConfig : public QDialog
{
    Q_OBJECT

public:

    StealthXConfig(QWidget *parent = 0);
    ~StealthXConfig();

    void setModel(WalletModel *model);


private:
    Ui::StealthXConfig *ui;
    WalletModel *model;
    void configure(bool enabled, int coins, int rounds);

private slots:

    void clickBasic();
    void clickHigh();
    void clickMax();
};

#endif // STEALTHXCONFIG_H
