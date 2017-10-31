#include <QGuiApplication>
#include <QDebug>
#include <QScreen>

#include <iostream>

using namespace std;

template<bool enable, bool disable>
void dpiInfo(int argc, char *argv[])
{
	QGuiApplication::setAttribute(Qt::AA_EnableHighDpiScaling, enable);
	QGuiApplication::setAttribute(Qt::AA_DisableHighDpiScaling, disable);

	QGuiApplication app(argc, argv);

	auto screens = QGuiApplication::screens();

	cout << "Enable/Disable: " << enable << "/" << disable << "\n";
	cout << "Global pixel ratio: " << app.devicePixelRatio() << "\n";
	cout << "Screens: " << screens.size() << "\n";

	foreach(QScreen *screen, screens) {
		cout << "\t" << screen->name().toStdString() << ":\n";
		cout << "\t\tPhysical DPI: " << screen->physicalDotsPerInch() << "\n";
		cout << "\t\t Logical DPI: " << screen->logicalDotsPerInch() << "\n";
		cout << "\t\t pixel ratio: " << screen->devicePixelRatio() << "\n";
	}

}

int main(int argc, char *argv[])
{
	QString qt_version = QString("QT version: 0x") + QString::number(QT_VERSION, 16);

	cout << qt_version.toStdString() << "\n";

	dpiInfo<false, false>(argc, argv);
	dpiInfo<false, true>(argc, argv);
	dpiInfo<true, false>(argc, argv);
	dpiInfo<true, true>(argc, argv);

	return 0;
}
