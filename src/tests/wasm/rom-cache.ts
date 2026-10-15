import fs from 'node:fs';
import path from 'node:path';
import {refreshAsset} from './asset-cache.ts';

export type RomCache = {
  url: string; buildSha256: string; sha256: string; bytes: number;
};

// Cache blobs are generated deployment artifacts, tied to one exact WASM
// binary and ROM. The CPU also checks its ABI, policy and source dependencies.
export async function loadRomCache(directory: string, game: string, buildSha256: string,
  romSha256: string, memoryImpl: number, unsafeCode: number): Promise<{file: string; cache: RomCache} | null> {
  const manifest = path.join(directory, game, 'manifest.json');
  if (!fs.existsSync(manifest)) return null;
  const info = JSON.parse(fs.readFileSync(manifest, 'utf8'));
  if (info.version !== 1 || info.game !== game || info.buildSha256 !== buildSha256
      || info.romSha256 !== romSha256 || info.memoryImpl !== memoryImpl || info.unsafeCode !== unsafeCode) return null;
  const file = path.join(directory, game, 'rom-cache.bin');
  const asset = await refreshAsset(file);
  if (asset.digest !== info.sha256 || asset.size !== info.bytes) throw Error('ROM translation cache content mismatch: ' + game);
  return {file, cache:{url:'/preload/rom-cache/' + game, buildSha256, sha256:asset.digest, bytes:asset.size}};
}
