#include <stdio.h>
#include <QFile>
#include <QTextStream>
#include <QMapIterator>
#include <QRect>

#include "outputgen.h"
#include "camres.h"

#include <QDebug>

#define S(n) QString(" ").repeated(n)

OutputGen::OutputGen(QObject *parent) :
    QObject(parent)
{
}

void OutputGen::dump(const QList<QPair<QString, int> > &cameras, const QList<QList<QPair<QString, QStringList> > > &resolutions)
{
    int i, j, m;

    for (i=0 ; i<cameras.size() ; i++)
    {
        if (resolutions.at(i).isEmpty())
        {
            qCritical("Camres warning: No resolutions found for %s (%d):", qPrintable(cameras.at(i).first), cameras.at(i).second);
            continue;
        }

        qInfo("\nResolutions for %s:", qPrintable(cameras.at(i).first));

        for (j=0 ; j<resolutions.at(i).size() ; j++)
        {
            qInfo("%s resolutions:", qPrintable(resolutions.at(i).at(j).first.split("-").first()));

            QStringList res = resolutions.at(i).at(j).second;

            for (m=0 ; m<res.size() ; m++)
            {
                qInfo("%s (%s)", qPrintable(res.at(m)), qPrintable(Camres::aspectRatioForResolution(res.at(m))));
            }
        }
    }
}

void OutputGen::makeJson(const QList<QPair<QString, int> > &cameras,
                         const QList<QList<QPair<QString, QStringList> > > &resolutions,
                         const QRect &screenGeometry,
                         const QString &filename)
{
    int i, j, m;
    QFile file;
    QTextStream *ts = NULL;

    file.setFileName(filename);

    if ( !file.open(QIODevice::ReadWrite | QIODevice::Truncate | QIODevice::Text))
    {
        qCritical("Camres error: Could not create output file.");
        return;
    }

    ts = new QTextStream(&file);

    qInfo("Camres: Writing json to file %s", qPrintable(file.fileName()));

    *ts << "{" << endl;

    for (i=0 ; i<cameras.size() ; i++)
    {
        if (resolutions.at(i).isEmpty())
        {
            qWarning("Camres warning: No resolutions found for %s (%d): Configuration not set.", qPrintable(cameras.at(i).first), cameras.at(i).second);
            continue;
        }

        if (i>0)
            *ts << "," << endl;

        *ts << S(4) << "\"" << cameras.at(i).first.split(" ").first().toLower() << "\":" << endl << S(4) << "{" << endl;

        for (j=0 ; j<resolutions.at(i).size() ; j++)
        {
            if (resolutions.at(i).at(j).first.startsWith("viewfinder"))
            {
                continue;
            }

            if (j>0)
                *ts << "," << endl;

            QStringList res = resolutions.at(i).at(j).second;

            *ts << S(8) << "\"" << resolutions.at(i).at(j).first.split("-").first().toLower() << "\":" << endl << S(8) << "[" << endl;
            QStringList repeatCheck;
            for (m=0 ; m<res.size() ; m++)
            {
                QString thisRes = res.at(m).split('@').first();
                if (repeatCheck.contains(thisRes)) continue;
                *ts << S(12) << "{ \"resolution\": \"" << thisRes << "\", "
                   << "\"viewFinder\": \"" << Camres::findBestViewFinderForResolution(thisRes, resolutions.at(i), screenGeometry) << "\", "
                   << "\"aspectRatio\": \"" << Camres::aspectRatioForResolution(thisRes) << "\" }"
                   << ((m == res.size()-1) ? "" : ",") << endl;
                repeatCheck << thisRes;
            }

            *ts << S(8) << "]";
        }

        *ts << endl;
        *ts << S(4) << "}";
    }

    *ts << endl;
    *ts << "}" << endl;

    file.close();
}

void OutputGen::makeCamhw(const QList<QPair<QString, int> > &cameras,
                          const QList<QList<QPair<QString, QStringList> > > &resolutions,
                          const QRect &screenGeometry,
                          const QString &filename)
{
    int i, j, m;

    QMap<QString, QString> map;

    QFile file;
    QTextStream *ts = NULL;
    file.setFileName(filename);

    if ( !file.open(QIODevice::ReadWrite | QIODevice::Truncate | QIODevice::Text))
    {
        qCritical("Camres error: Could not create output file.");
        return;
    }

    ts = new QTextStream(&file);

    qInfo("Camres: Writing dconf settings to file %s", qPrintable(file.fileName()));

    QFile resfile("/usr/share/droid-camres/jolla-camera-hw-template.txt");
    QStringList camhwTemplate;

    if (!resfile.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        qCritical("Camres error: failed to open template");
        return;
    }

    QTextStream rests(&resfile);

    while(!rests.atEnd())
    {
        camhwTemplate.append(rests.readLine());
    }

    resfile.close();

    for (i=0 ; i<cameras.size() ; i++)
    {
        if (resolutions.at(i).isEmpty())
        {
            qWarning("Camres warning: No resolutions found for %s (%d): Configuration not set.", qPrintable(cameras.at(i).first), cameras.at(i).second);
            continue;
        }

        QString camKey = cameras.at(i).first.left(3).toUpper();
        for (j=0 ; j<resolutions.at(i).size() ; j++)
        {
            QString resType = resolutions.at(i).at(j).first;
            QStringList res = resolutions.at(i).at(j).second;
            QString prefix;
            bool isVideo = false;

            if (resType.startsWith("viewfinder"))
            {
                prefix = "@" + camKey + "VF";
            }
            else if (resType.startsWith("image"))
            {
                prefix = "@" + camKey + "IMAGE";
            }
            else if (resType.startsWith("video"))
            {
                prefix = "@" + camKey + "VIDEO";
                isVideo = true;
            }
            else continue; // unknown resolution type

            QMap<QString, int> sizes;
            int topFramerate = 0;
            for (m=0 ; m<res.size() ; m++)
            {
                QList<QString> resBits = res.at(m).split(QRegExp("[x@\\-\\/]"));
                int size = resBits.at(0).toInt() * resBits.at(1).toInt();
                if (!resType.startsWith("viewfinder") || (
                    qMin(screenGeometry.height(), screenGeometry.width()) >=
                    qMin(resBits.at(0).toInt(), resBits.at(1).toInt()) &&
                    qMax(screenGeometry.height(), screenGeometry.width()) >=
                    qMax(resBits.at(0).toInt(), resBits.at(1).toInt())))
                {
                    QString aspect = "";
                    if (Camres::aspectRatioForResolution(res.at(m)).compare("4:3") == 0)
                    {
                        if (isVideo) continue;
                        aspect = "43";
                    }
                    else if (Camres::aspectRatioForResolution(res.at(m)).compare("16:9") == 0)
                    {
                        if (!isVideo) aspect = "169";
                    }
                    else continue;
                    int framerate = 0;
                    if (isVideo)
                    {
                        switch (resBits.size())
                        {
                        case 4:
                            framerate = resBits.at(2).toInt()/resBits.at(3).toInt();
                            break;
                        case 6: // take the top of the range
                            framerate = resBits.at(4).toInt()/resBits.at(5).toInt();
                            break;
                        default:
                            // video framerate without fps. skip
                            continue;
                        }
                    }
                    QString key = prefix + aspect + "RES@";
                    if ((map.value(key).isEmpty() || size >= sizes.value(key)) && framerate >= topFramerate)
                    {
                        map.insert(key, resBits.at(0)+"x"+resBits.at(1));
                        sizes.insert(key, size);
                        if (isVideo)
                        {
                            map.insert(prefix+"FPS@", QString::number(framerate));
                            topFramerate = framerate;
                        }
                    }
                }
            }
        }
    }

    QMapIterator<QString, QString> k(map);

    while (k.hasNext())
    {
        k.next();
        if (k.value().isEmpty())
            qCritical("Camres error: Not found suitable resolution for %s. Check output!", qPrintable(k.key()));
        else
            camhwTemplate.replaceInStrings(k.key(), k.value());
    }

    for (i=0 ; i<camhwTemplate.size() ; i++)
        *ts << camhwTemplate.at(i) << endl;

    file.close();
}
