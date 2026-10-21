#include <QCoreApplication>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    const QStringList arguments = application.arguments();
    if (arguments.size() < 2) {
        return 2;
    }
    QSaveFile output(arguments.at(1));
    if (!output.open(QIODevice::WriteOnly)) {
        return 3;
    }
    QJsonArray values;
    for (const QString& argument : arguments.mid(2)) {
        values.append(argument);
    }
    const QByteArray payload =
        QJsonDocument(QJsonObject{{QStringLiteral("arguments"), values},
                                  {QStringLiteral("workingDirectory"), QDir::currentPath()}})
            .toJson();
    return output.write(payload) == payload.size() && output.commit() ? 0 : 4;
}
