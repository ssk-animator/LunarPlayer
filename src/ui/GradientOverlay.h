#pragma once
#include <QWidget>

class GradientOverlay : public QWidget {
    Q_OBJECT
public:
    explicit GradientOverlay(QWidget *parent = nullptr);

protected:
    void paintEvent(QPaintEvent *event) override;
};
