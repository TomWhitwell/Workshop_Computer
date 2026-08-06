(function(){
  var button=document.querySelector('.site-menu-toggle');
  var nav=document.getElementById('site-nav');
  if(!button||!nav)return;
  document.body.classList.add('site-menu-enabled');
  function setOpen(open){button.setAttribute('aria-expanded',String(open));nav.classList.toggle('is-open',open);}
  button.addEventListener('click',function(){setOpen(button.getAttribute('aria-expanded')!=='true');});
  nav.addEventListener('click',function(e){if(e.target.closest('a'))setOpen(false);});
  document.addEventListener('keydown',function(e){if(e.key==='Escape'&&button.getAttribute('aria-expanded')==='true'){setOpen(false);button.focus();}});
})();
