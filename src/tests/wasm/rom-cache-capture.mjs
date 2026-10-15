import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';

export async function configureRomCacheCapture(page) {
  await page.evaluate(() => {
    const build = new URL(window.ekaAssetUrls['/eka2l1.wasm'],location.href).searchParams.get('v');
    if (Module.ccall('eka2l1_rom_cache_configure','number',['string','string'],['',build]) !== 0)
      throw Error('ROM cache capture configuration failed');
  });
}

// Export retranslates ROM entries. Call after timing and memory collection.
export async function captureRomCache(page, output, game) {
  const metadata = await page.evaluate(() => ({
    buildSha256:new URL(window.ekaAssetUrls['/eka2l1.wasm'],location.href).searchParams.get('v'),
    romSha256:new URL(window.ekaAssetUrls['/preload/rom'],location.href).searchParams.get('v'),
    memoryImpl:Module._eka2l1_memory_impl_report(),unsafeCode:Module._eka2l1_unsafe_code_report(),
  }));
  const target='/tmp/rom-cache-export.bin';
  if (await page.evaluate(target=>Module.ccall('eka2l1_rom_cache_export','number',['string'],[target]),target) !== 0)
    throw Error('ROM cache export request failed');
  await page.evaluate(() => {
    if (Module._eka2l1_route_phase()===1)
      Module._eka2l1_route_step_to(Math.min(1800000000,Module._eka2l1_guest_time_us()+10000000));
  });
  await page.waitForFunction(()=>Module._eka2l1_rom_cache_status()!==1,{timeout:120000});
  if (await page.evaluate(()=>Module._eka2l1_rom_cache_status())!==2) throw Error('ROM cache export failed');
  const bytes = await page.evaluate(target => {
    window.ekaRomCacheExport = FS.readFile(target);return window.ekaRomCacheExport.length;
  },target);
  const file=path.join(output,'rom-cache.bin'), descriptor=fs.openSync(file,'w');
  try {
    for (let offset=0;offset<bytes;offset+=256*1024) {
      const encoded=await page.evaluate(({offset,size})=>{
        const data=window.ekaRomCacheExport.subarray(offset,offset+size);let text='';
        for(let i=0;i<data.length;i+=32768)text+=String.fromCharCode(...data.subarray(i,i+32768));
        return btoa(text);
      },{offset,size:256*1024});
      fs.writeSync(descriptor,Buffer.from(encoded,'base64'));
    }
  } finally {
    fs.closeSync(descriptor);
    await page.evaluate(target=>{delete window.ekaRomCacheExport;FS.unlink(target);},target);
  }
  const sha256=crypto.createHash('sha256');
  for await (const chunk of fs.createReadStream(file))sha256.update(chunk);
  const manifest={version:1,game,...metadata,sha256:sha256.digest('hex'),bytes};
  fs.writeFileSync(path.join(output,'manifest.json'),JSON.stringify(manifest,null,2)+'\n');
  return manifest;
}
