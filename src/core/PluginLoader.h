#pragma once
#include <QObject>
#include <QPluginLoader>
#include <QMap>
#include <QList>
#include <QString>
#include <QWidget>
#include "shared/plugin_interface.h"

struct LoadedPlugin {
    QString filePath;
    QPluginLoader* loader;
    IChannelPlugin* instance;
    QWidget* widget;
    QList<QMetaObject::Connection> connections;
};

class PluginLoader : public QObject {
    Q_OBJECT
public:
    PluginLoader(QObject* parent = nullptr);
    ~PluginLoader();

    // plugins/ ディレクトリ内の DLL を走査する
    QList<QString> scanPlugins();

    // 個別のプラグインをロードする
    IChannelPlugin* loadPlugin(const QString& filePath, ICoreContext* context, QWidget* uiParent = nullptr);

    // 個別のプラグインをアンロードする
    bool unloadPlugin(const QString& filePath);

    // 現在ロードされているプラグインのリストを取得
    QList<LoadedPlugin> loadedPlugins() const;

protected:
    void extractDefaultAssets(IChannelPlugin* plugin);

private:

    QMap<QString, LoadedPlugin> m_loadedPlugins; // キー: DLLの絶対パス
};
