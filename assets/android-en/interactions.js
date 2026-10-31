// Presentation-only interactions. No VPN, filesystem, network or persistent storage access.
document.querySelectorAll('[data-scenario]').forEach(el=>el.addEventListener('change',()=>location.href=el.value));
document.querySelectorAll('[data-reveal]').forEach(el=>el.addEventListener('click',()=>{
  const field=document.getElementById(el.dataset.reveal);
  field.type=field.type==='password'?'text':'password';
  el.textContent=field.type==='password'?'Show':'Hide';
  el.setAttribute('aria-pressed',String(field.type==='text'));
}));
document.querySelectorAll('[data-toast]').forEach(el=>el.addEventListener('click',()=>{
  const box=el.closest('.device').querySelector('.toast');
  box.textContent=el.dataset.toast;box.hidden=false;
  clearTimeout(box.timer);box.timer=setTimeout(()=>box.hidden=true,4500);
}));
document.querySelectorAll('[data-disables]').forEach(el=>{
  const update=()=>{document.getElementById(el.dataset.disables).disabled=el.checked;};
  el.addEventListener('change',update);update();
});
document.querySelectorAll('[data-fill]').forEach(el=>el.addEventListener('click',()=>{
  document.getElementById(el.dataset.target).value=el.dataset.fill;
}));
