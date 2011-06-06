extern "C" {
#include "heheader.h"
}
#include "aboutdialog.h"
#include "ui_aboutdialog.h"

AboutDialog::AboutDialog( QWidget* parent )
    : QDialog(parent, Qt::WindowTitleHint | Qt::WindowSystemMenuHint),
      ui(new Ui::AboutDialog)
{
    ui->setupUi(this);

    // Make the dialog fixed-size.
    this->setMaximumSize(this->minimumSize());

    // Construct a string holding all version info.
    QString str("<p>Hugo engine v");
    str += QString::number(HEVERSION) + "." + QString::number(HEREVISION)
           + HEINTERIM + "</p>";
    ui->label->setText(ui->label->text() + str);
}


AboutDialog::~AboutDialog()
{
    delete ui;
}