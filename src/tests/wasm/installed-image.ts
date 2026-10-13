import fs from 'node:fs';
import path from 'node:path';
import {refreshAsset} from './asset-cache.ts';

export async function loadInstalledImage(directory: string, game: string, sources: Record<string,string>) {
  const metadataPath = path.join(directory,game+'.json');
  if (!fs.existsSync(metadataPath)) return undefined;
  const metadata = JSON.parse(fs.readFileSync(metadataPath,'utf8'));
  if (metadata.version !== 1 || metadata.game !== game || !metadata.sources
      || Object.keys(metadata.sources).length !== Object.keys(sources).length
      || Object.entries(sources).some(([key,value]) => metadata.sources[key] !== value)
      || metadata.file !== game+'.img.gz' || !/^[a-f0-9]{64}$/.test(metadata.sha256))
    throw Error('Installed image source/version mismatch: ' + metadataPath);
  const file = path.join(directory,metadata.file);
  const asset = await refreshAsset(file);
  if (asset.digest !== metadata.sha256) throw Error('Installed image SHA-256 mismatch: ' + file);
  return {file, image:{url:'/preload/games/'+game+'/image',game,sources}};
}
