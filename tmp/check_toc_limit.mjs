import { loadQiModule } from '../test/web-harness/load-qi.mjs';
const m = await loadQiModule('./html');
const out = m.ccall('qi_web_build', 'string', ['string'], ["qi % -f negroni.go --toc"]);
for (const line of out.split('\n')) {
  if (/^(MODE|LIMIT|ERROR|TOC_INCLUDES)\|/.test(line)) console.log(line);
}
