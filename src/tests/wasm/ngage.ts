// Restore Nokia's multipart distribution from its unchanged signed SIS parts.
// The launcher checks the complete result against the original CD package hash.
export function ngagePackage(metadata: Uint8Array, game: Uint8Array): Buffer {
  const boundary = '--KBoundary\r\n';
  const sisHeaders = 'Content-Type: application/sis; charset="iso-8859-1"\r\n'
    + 'Content-Transfer-Encoding: binary\r\n';
  const retailer = '<?xml version="1.0" encoding="UTF-8"?>'
    + '<par:RetailerInfo xmlns:par="http://www.n-gage.nokia.com/schemas/DPB/RetailerInfo/1.0">'
    + '<par:RetailerName>Nokia Shop</par:RetailerName><par:RetailerID>4</par:RetailerID>'
    + '<par:ProtocolType>1</par:ProtocolType><par:NGageAccountSupport>1</par:NGageAccountSupport>'
    + '</par:RetailerInfo>';
  return Buffer.concat([
    Buffer.from('Content-Type: multipart/mixed; boundary="KBoundary";\r\n'
      + 'MIME-Version: 1.0\r\nContent-Description: Ngage-Install-File; version="1.0"\r\n'
      + boundary + 'Content-Type: text/xml; charset="UTF-8"\r\nContent-ID: <RetailerInfo>\r\n\r\n'
      + retailer + '\r\n' + boundary + sisHeaders + 'Content-ID: <Metadata>\r\n\r\n'),
    metadata,
    Buffer.from('\r\n' + boundary + sisHeaders + 'Content-ID: <Game>\r\n\r\n'),
    game,
    Buffer.from('\r\n--KBoundary--\r\n'),
  ]);
}
