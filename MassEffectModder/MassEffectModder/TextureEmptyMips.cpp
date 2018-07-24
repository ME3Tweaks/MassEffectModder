/*
 * MassEffectModder
 *
 * Copyright (C) 2018 Pawel Kolodziejski <aquadran at users.sourceforge.net>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 */

#include "MipMaps.h"
#include "GameData.h"
#include "Texture.h"
#include "Helpers/MiscHelpers.h"

QList<RemoveMipsEntry> *MipMaps::prepareListToRemove(QList<FoundTexture> *textures)
{
    auto list = new QList<RemoveMipsEntry>();

    for (int k = 0; k < textures->count(); k++)
    {
        for (int t = 0; t < textures->at(k).list->count(); t++)
        {
            if (textures->at(k).list->at(t).path == "")
                continue;
            if (textures->at(k).list->at(t).removeEmptyMips)
            {
                bool found = false;
                for (int e = 0; e < list->count(); e++)
                {
                    if (list->at(e).pkgPath == textures->at(k).list->at(t).path)
                    {
                        RemoveMipsEntry entry = list->at(e);
                        int exportId = textures->at(k).list->at(t).exportID;
                        entry.exportIDs.push_back(exportId);
                        list->replace(e, entry);
                        found = true;
                        break;
                    }
                }
                if (found)
                    continue;
                RemoveMipsEntry entry{};
                entry.pkgPath = textures->at(k).list->at(t).path;
                entry.exportIDs.push_back(textures->at(k).list->at(t).exportID);
                list->push_back(entry);
            }
        }
    }

    return list;
}

void MipMaps::removeMipMapsME1(int phase, QList<FoundTexture> *textures, QStringList *pkgsToMarker, bool ipc)
{
    int lastProgress = -1;

    QList<RemoveMipsEntry> *list = prepareListToRemove(textures);
    QString path = "/BioGame/CookedPC/testVolumeLight_VFX.upk";
    for (int i = 0; i < list->count(); i++)
    {
        if (path.compare(list->at(i).pkgPath, Qt::CaseInsensitive))
            continue;

        if (ipc)
        {
            int newProgress = (list->count() * (phase - 1) + i + 1) * 100 / (list->count() * 2);
            if (lastProgress != newProgress)
            {
                ConsoleWrite(QString("[IPC]TASK_PROGRESS ") + QString::number(newProgress));
                ConsoleSync();
                lastProgress = newProgress;
            }
        }

        Package package{};
        if (package.Open(g_GameData->GamePath() + list->at(i).pkgPath) != 0)
        {
            if (ipc)
            {
                ConsoleWrite(QString("[IPC]ERROR Issue opening package file: ") + list->at(i).pkgPath);
                ConsoleSync();
            }
            else
            {
                QString err;
                err += "---- Start --------------------------------------------\n";
                err += "Issue opening package file: " + list->at(i).pkgPath + "\n";
                err += "---- End ----------------------------------------------\n\n";
                ConsoleWrite(err);
            }
            delete  list;
            return;
        }

        removeMipMapsME1(phase, textures, package, list, pkgsToMarker, i, ipc);
    }
    delete  list;
}

void MipMaps::removeMipMapsME1(int phase, QList<FoundTexture> *textures, Package &package,
                               QList<RemoveMipsEntry> *list, QStringList *pkgsToMarker,
                               int removeEntry, bool ipc)
{
    for (int l = 0; l < list->at(removeEntry).exportIDs.count(); l++)
    {
        int exportID = list->at(removeEntry).exportIDs[l];
        Texture *texture = new Texture(package, exportID, package.getExportData(exportID), false);
        if (!texture->hasEmptyMips())
        {
            delete texture;
            continue;
        }
        texture->removeEmptyMips();
        texture->properties->setIntValue("SizeX", texture->mipMapsList.first().width);
        texture->properties->setIntValue("SizeY", texture->mipMapsList.first().height);
        texture->properties->setIntValue("MipTailBaseIdx", texture->mipMapsList.count() - 1);

        int foundListEntry = -1;
        int foundTextureEntry = -1;
        QString pkgName = package.packagePath.toLower();
        for (int k = 0; k < textures->count(); k++)
        {
            for (int t = 0; t < textures->at(k).list->count(); t++)
            {
                if (textures->at(k).list->at(t).exportID == exportID &&
                    textures->at(k).list->at(t).path.toLower() == pkgName)
                {
                    foundTextureEntry = k;
                    foundListEntry = t;
                    break;
                }
            }
        }
        if (foundListEntry == -1)
        {
            if (ipc)
            {
                ConsoleWrite(QString("[IPC]ERROR Texture ") + package.exportsTable[exportID].objectName +
                             " not found in tree: " + list->at(removeEntry).pkgPath + ", skipping...");
                ConsoleSync();
            }
            else
            {
                ConsoleWrite(QString("Error: Texture ") + package.exportsTable[exportID].objectName +
                             " not found in package: " + list->at(removeEntry).pkgPath + ", skipping...\n");
            }
            delete texture;
            continue;
        }

        MatchedTexture m = textures->at(foundTextureEntry).list->at(foundListEntry);
        if (m.linkToMaster != -1)
        {
            if (phase == 1)
            {
                delete texture;
                continue;
            }

            const MatchedTexture& foundMasterTex = textures->at(foundTextureEntry).list->at(m.linkToMaster);
            if (texture->mipMapsList.count() != foundMasterTex.masterDataOffset->count())
            {
                if (ipc)
                {
                    ConsoleWrite(QString("[IPC]ERROR Texture ") + package.exportsTable[exportID].objectName + " in package: " + foundMasterTex.path + " has wrong reference, skipping...");
                    ConsoleSync();
                }
                else
                {
                    ConsoleWrite(QString("Error: Texture ") + package.exportsTable[exportID].objectName + " in package: " + foundMasterTex.path + " has wrong reference, skipping...\n");
                }
                delete texture;
                continue;
            }
            for (int t = 0; t < texture->mipMapsList.count(); t++)
            {
                Texture::TextureMipMap mipmap = texture->mipMapsList[t];
                if (mipmap.storageType == Texture::StorageTypes::extLZO ||
                    mipmap.storageType == Texture::StorageTypes::extZlib ||
                    mipmap.storageType == Texture::StorageTypes::extUnc)
                {
                    mipmap.dataOffset = foundMasterTex.masterDataOffset->at(t);
                    texture->mipMapsList[t] = mipmap;
                }
            }
        }

        uint packageDataOffset;
        {
            MemoryStream newData{};
            newData.WriteFromBuffer(texture->properties->toArray());
            packageDataOffset = package.exportsTable[exportID].getDataOffset() + (uint)newData.Position();
            newData.WriteFromBuffer(texture->toArray(packageDataOffset));
            package.setExportData(exportID, newData.ToArray());
        }

        if (m.linkToMaster == -1)
        {
            if (phase == 2)
                CRASH();
            m.masterDataOffset = new QList<uint>();
            for (int t = 0; t < texture->mipMapsList.count(); t++)
            {
                m.masterDataOffset->push_back(packageDataOffset + texture->mipMapsList[t].internalOffset);
            }
        }

        m.removeEmptyMips = false;
        textures->at(foundTextureEntry).list->replace(foundListEntry, m);
        delete texture;
    }
    if (package.SaveToFile(false, false, true))
    {
        pkgsToMarker->removeOne(package.packagePath);
    }
}

void MipMaps::removeMipMapsME2ME3(QList<FoundTexture> *textures, QStringList *pkgsToMarker,
                                  QStringList *pkgsToRepack, bool ipc, bool repack)
{
    int lastProgress = -1;
    QList<RemoveMipsEntry> *list = prepareListToRemove(textures);
    QString path;
    if (GameData::gameType == MeType::ME2_TYPE)
    {
        path = g_GameData->GamePath() + "/BioGame/CookedPC/BIOC_Materials.pcc";
    }
    for (int i = 0; i < list->count(); i++)
    {
        if (path.compare(list->at(i).pkgPath, Qt::CaseInsensitive))
            continue;

        if (ipc)
        {
            int newProgress = (i + 1) * 100 / list->count();
            if (lastProgress != newProgress)
            {
                ConsoleWrite(QString("[IPC]TASK_PROGRESS ") + QString::number(newProgress));
                ConsoleSync();
                lastProgress = newProgress;
            }
        }

        Package package{};
        if (package.Open(g_GameData->GamePath() + list->at(i).pkgPath) != 0)
        {
            if (ipc)
            {
                ConsoleWrite(QString("[IPC]ERROR Issue opening package file: ") + list->at(i).pkgPath);
                ConsoleSync();
            }
            else
            {
                QString err;
                err += "---- Start --------------------------------------------\n";
                err += "Issue opening package file: " + list->at(i).pkgPath + "\n";
                err += "---- End ----------------------------------------------\n\n";
                ConsoleWrite(err);
            }
            delete  list;
            return;
        }

        removeMipMapsME2ME3(package, list, pkgsToMarker, pkgsToRepack, i, repack);
    }
    delete  list;
}

void MipMaps::removeMipMapsME2ME3(Package &package, QList<RemoveMipsEntry> *list,
                                  QStringList *pkgsToMarker, QStringList *pkgsToRepack,
                                  int removeEntry, bool repack)
{
    for (int l = 0; l < list->at(removeEntry).exportIDs.count(); l++)
    {
        int exportID = list->at(removeEntry).exportIDs[l];
        Texture *texture = new Texture(package, exportID, package.getExportData(exportID), false);
        if (!texture->hasEmptyMips())
        {
            delete texture;
            continue;
        }
        texture->removeEmptyMips();
        texture->properties->setIntValue("SizeX", texture->mipMapsList.first().width);
        texture->properties->setIntValue("SizeY", texture->mipMapsList.first().height);
        texture->properties->setIntValue("MipTailBaseIdx", texture->mipMapsList.count() - 1);

        {
            MemoryStream newData{};
            newData.WriteFromBuffer(texture->properties->toArray());
            newData.WriteFromBuffer(texture->toArray(package.exportsTable[exportID].getDataOffset() +
                                                     (uint)newData.Position()));
            package.setExportData(exportID, newData.ToArray());
        }
        delete texture;
    }

    if (package.SaveToFile(repack, false, true))
    {
        if (repack)
            pkgsToRepack->removeOne(package.packagePath);
        pkgsToMarker->removeOne(package.packagePath);
    }
}