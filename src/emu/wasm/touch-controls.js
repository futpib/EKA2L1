// Touch input shares the keyboard's source ownership, so releasing one finger
// never releases a key still held by another input device.
class EkaTouchControls {
    constructor({press, release, releaseAll, sources}) {
        this.press = press; this.release = release; this.releaseAll = releaseAll; this.sources = sources;
        this.profile = 'generic'; this.editing = false; this.pointers = new Map(); this.owned = new Set();
        this.deck = document.getElementById('touch-deck');
        this.deck.innerHTML = `
            <div class="touch-zone" id="move-zone" aria-label="Movement area">
                <div class="touch-cluster" id="movement-pad" role="group" aria-label="Directional controls">
                    <button type="button" data-scan="16" aria-label="Up">↑</button>
                    <button type="button" data-scan="14" aria-label="Left">←</button>
                    <button type="button" data-scan="15" aria-label="Right">→</button>
                    <button type="button" data-scan="17" aria-label="Down">↓</button>
                    <span id="stick-knob"></span>
                </div>
            </div>
            <div class="touch-zone" id="action-zone"><div class="touch-cluster" id="action-group">
                    <button type="button" data-scan="164" aria-label="Left softkey" title="Left softkey">L</button>
                    <button type="button" data-scan="165" aria-label="Right softkey" title="Right softkey">R</button>
                <button type="button" id="primary-action" data-scan="167">Select</button>
            </div></div>`;
        document.body.insertAdjacentHTML('beforeend', `
            <dialog id="touch-settings" aria-labelledby="touch-settings-title">
                <button class="dialog-close" data-close aria-label="Close controls">×</button>
                <h2 id="touch-settings-title">Touch controls</h2><p id="touch-profile"></p>
                <label>Movement <select data-pref="mode"><option value="pad">Fixed directional pad</option><option value="stick">Floating stick</option></select></label>
                <label>Directions <select data-pref="directions"><option value="4">Four directions</option><option value="8">Eight directions</option></select></label>
                <label>Control size <input aria-label="Control size" data-pref="size" type="range" min="120" max="190" step="2"></label>
                <label>Spacing <input aria-label="Spacing" data-pref="gap" type="range" min="4" max="40" step="2"></label>
                <label>Opacity <input aria-label="Opacity" data-pref="opacity" type="range" min="0.35" max="1" step="0.05"></label>
                <label>Left-handed layout <input data-pref="leftHanded" type="checkbox"></label>
                <label>Action button <select data-pref="action"><option value="167">Select / joystick press</option><option value="141">5</option><option value="146">0</option><option value="133">*</option><option value="127">#</option></select></label>
                <label>Tap action to hold / release <input data-pref="latch" type="checkbox"></label>
                <p>The game keeps running while you edit. Hold mode releases when you open controls, switch apps or leave this page.</p>
                <div class="dialog-actions"><button id="edit-touch-layout">Move controls</button><button id="reset-touch-layout">Reset</button><button data-close>Done</button></div>
            </dialog>
            <dialog id="phone-keypad" aria-labelledby="keypad-title"><h2 id="keypad-title">Phone keypad</h2>
                <div class="key-grid"></div><div class="dialog-actions"><button data-close>Close</button></div></dialog>`);
        this.settings = document.getElementById('touch-settings');
        this.keypad = document.getElementById('phone-keypad');
        this.menu = document.getElementById('session-panel');
        this.menuButton = document.getElementById('btn-game-menu');
        this.moveZone = document.getElementById('move-zone'); this.pad = document.getElementById('movement-pad');
        this.actionZone = document.getElementById('action-zone'); this.actions = document.getElementById('action-group');
        this.primary = document.getElementById('primary-action'); this.knob = document.getElementById('stick-knob');
        for (const [label, scan] of [['1',137],['2',138],['3',139],['4',140],['5',141],['6',142],['7',143],['8',144],['9',145],['*',133],['0',146],['#',127],['Left softkey',164],['Select',167],['Right softkey',165],['Clear',1]]) {
            const button = document.createElement('button'); button.type = 'button'; button.textContent = label;
            button.dataset.scan = scan; this.keypad.querySelector('.key-grid').append(button);
        }
        document.getElementById('btn-touch-settings').onclick = () => {
            if (this.editing) { this.setEditing(false); return; }
            this.open(this.settings);
        };
        document.getElementById('btn-keypad').onclick = () => { this.setEditing(false); this.open(this.keypad); };
        this.menuButton.onclick = () => {
            if (this.editing) this.setEditing(false); else this.open(this.menu);
        };
        const fullscreen = document.getElementById('btn-fullscreen');
        fullscreen.hidden = !document.fullscreenEnabled;
        fullscreen.onclick = async () => {
            try {
                if (document.fullscreenElement) await document.exitFullscreen();
                else await document.documentElement.requestFullscreen();
                this.menu.close();
            } catch (_) { fullscreen.textContent = 'Full screen unavailable'; }
        };
        document.addEventListener('fullscreenchange', () => {
            fullscreen.textContent = document.fullscreenElement ? 'Exit full screen' : 'Full screen';
        });
        for (const dialog of [this.settings, this.keypad, this.menu]) {
            for (const button of dialog.querySelectorAll('[data-close]')) button.onclick = () => dialog.close();
            dialog.addEventListener('close', () => { this.releaseAll(); document.getElementById('canvas').focus(); });
            dialog.addEventListener('cancel', event => {
                this.releaseAll();
                if (dialog === this.menu && !window._gameRunning) event.preventDefault();
            });
        }
        for (const area of [this.deck, this.keypad]) area.addEventListener('contextmenu', event => event.preventDefault());
        this.settings.querySelectorAll('[data-pref]').forEach(input => input.addEventListener('input', () => {
            this.releaseAll();
            this.prefs[input.dataset.pref] = input.type === 'checkbox' ? input.checked
                : input.dataset.pref === 'mode' ? input.value : Number(input.value);
            this.save(); this.layout();
        }));
        document.getElementById('reset-touch-layout').onclick = () => {
            this.releaseAll(); this.prefs = this.defaults(); this.save(); this.fillSettings(); this.layout();
        };
        document.getElementById('edit-touch-layout').onclick = () => { this.settings.close(); this.setEditing(true); };
        this.moveZone.addEventListener('pointerdown', event => this.moveStart(event));
        this.moveZone.addEventListener('pointermove', event => this.move(event));
        for (const name of ['pointerup', 'pointercancel', 'lostpointercapture']) {
            this.moveZone.addEventListener(name, event => this.moveEnd(event));
        }
        for (const button of [...this.actions.querySelectorAll('button'), ...this.keypad.querySelectorAll('button[data-scan]')]) this.bindButton(button);
        for (const button of this.pad.querySelectorAll('button')) button.addEventListener('click', event => {
            if (event.detail === 0 && !this.editing) this.pulse(Number(button.dataset.scan));
        });
        for (const [group, zone, key] of [[this.pad,this.moveZone,'move'],[this.actions,this.actionZone,'buttons']]) {
            group.addEventListener('pointerdown', event => {
                if (!this.editing || event.button !== 0 || this.drag) return;
                event.preventDefault(); event.stopImmediatePropagation(); group.setPointerCapture(event.pointerId);
                const rect = group.getBoundingClientRect(); this.drag = {id:event.pointerId,group,zone,key,x:event.clientX-rect.left,y:event.clientY-rect.top};
            }, true);
            group.addEventListener('pointermove', event => {
                if (!this.drag || this.drag.id !== event.pointerId) return;
                const d=this.drag, z=d.zone.getBoundingClientRect(), g=d.group.getBoundingClientRect();
                this.prefs[d.key] = {x:this.clamp((event.clientX-z.left-d.x)/Math.max(1,z.width-g.width),0,1), y:this.clamp((event.clientY-z.top-d.y)/Math.max(1,z.height-g.height),0,1)};
                this.layout(); this.save();
            });
            for (const name of ['pointerup','pointercancel','lostpointercapture']) group.addEventListener(name, event => {
                if (this.drag?.id === event.pointerId) this.drag = null;
            });
        }
        new ResizeObserver(() => { this.releaseAll(); this.layout(); }).observe(this.deck);
        this.configure('');
        this.menu.showModal();
    }
    clamp(value,min,max) { return Math.min(max,Math.max(min,value)); }
    defaults() {
        return {mode:this.profile==='snakes'?'pad':'stick', directions:this.profile==='snakes'?4:8,
            size:132,gap:12,opacity:.55,leftHanded:false,action:167,latch:false,move:{x:.25,y:.75},buttons:{x:.75,y:.75}};
    }
    configure(app) {
        this.releaseAll();
        const name=String(app).trim().toLowerCase();
        this.profile=['0x2000730f','snakes'].includes(name)?'snakes':['0xa020d913','sky force','sky-force'].includes(name)?'sky-force':'generic';
        this.storageKey='eka-touch-v1:'+(this.profile==='generic'?(name||'generic'):this.profile);
        this.prefs=this.defaults();
        try {
            const saved=JSON.parse(localStorage.getItem(this.storageKey));
            if (saved && typeof saved==='object') {
                if (['pad','stick'].includes(saved.mode)) this.prefs.mode=saved.mode;
                if ([4,8].includes(saved.directions)) this.prefs.directions=saved.directions;
                if ([167,141,146,133,127].includes(saved.action)) this.prefs.action=saved.action;
                for (const [key,min,max] of [['size',120,190],['gap',4,40],['opacity',.35,1]])
                    if (Number.isFinite(saved[key])) this.prefs[key]=this.clamp(saved[key],min,max);
                for (const key of ['leftHanded','latch']) if (typeof saved[key]==='boolean') this.prefs[key]=saved[key];
                for (const key of ['move','buttons']) if (Number.isFinite(saved[key]?.x)&&Number.isFinite(saved[key]?.y))
                    this.prefs[key]={x:this.clamp(saved[key].x,0,1),y:this.clamp(saved[key].y,0,1)};
            }
        } catch (_) { /* Private browsing and unavailable storage retain usable defaults. */ }
        document.getElementById('touch-profile').textContent=(this.profile==='generic'?'General-purpose layout':this.profile==='snakes'?'Snakes':'Sky Force')+' · Saved on this device';
        this.fillSettings(); this.layout();
    }
    save() { try { localStorage.setItem(this.storageKey,JSON.stringify(this.prefs)); } catch (_) {} }
    startGame(app) {
        this.configure(app); this.menu.close();
        this.menu.querySelector('[data-close]').hidden=false;
        this.menuButton.hidden=false;
        document.getElementById('session-title').textContent=this.profile==='snakes'?'Snakes':this.profile==='sky-force'?'Sky Force':'Game options';
    }
    fillSettings() {
        for (const input of this.settings.querySelectorAll('[data-pref]')) {
            if (input.type==='checkbox') input.checked=this.prefs[input.dataset.pref]; else input.value=this.prefs[input.dataset.pref];
        }
    }
    open(dialog) {
        this.releaseAll();
        if (dialog!==this.menu) this.menu.close();
        dialog.showModal();
    }
    get modalOpen() { return this.settings.open || this.keypad.open || this.menu.open; }
    setEditing(value) {
        this.releaseAll(); this.editing=value; this.deck.classList.toggle('editing',value);
        document.getElementById('btn-touch-settings').textContent=value?'Done moving':'Controls';
        this.menuButton.innerHTML=value?'✓':'<span aria-hidden="true">☰</span>';
        this.menuButton.setAttribute('aria-label',value?'Finish moving controls':'Game options');
        document.getElementById('control-hint').classList.toggle('editing',value);
    }
    layout() {
        if (!this.prefs) return;
        const p=this.prefs, root=document.documentElement;
        this.deck.dataset.profile=this.profile; this.deck.dataset.mode=p.mode;
        this.deck.classList.toggle('left-handed',p.leftHanded);
        root.style.setProperty('--control-size',p.size+'px'); root.style.setProperty('--control-gap',p.gap+'px'); root.style.setProperty('--control-opacity',p.opacity);
        this.moveZone.style.order=p.leftHanded?2:0; this.moveZone.dataset.mode=p.mode;
        this.moveZone.style.overflow=p.mode==='stick'?'hidden':'visible';
        for (const [group,zone,key] of [[this.pad,this.moveZone,'move'],[this.actions,this.actionZone,'buttons']]) {
            const w=zone.clientWidth,h=zone.clientHeight, size=Math.max(1,Math.min(p.size,w,h));
            group.style.setProperty('--actual-size',size+'px');
            group.style.left=(w-size)*p[key].x+'px'; group.style.top=(h-size)*p[key].y+'px';
        }
        this.primary.dataset.scan=p.action;
        const label=p.action===167?(this.profile==='sky-force'?'Fire / OK':'Select'):({141:'5',146:'0',133:'*',127:'#'})[p.action];
        this.primary.innerHTML=p.action===167
            ? (this.profile==='sky-force'
                ? '<svg viewBox="0 0 32 32" aria-hidden="true"><circle cx="16" cy="16" r="9"/><path d="M16 2v8m0 12v8M2 16h8m12 0h8"/></svg>'
                : '<svg viewBox="0 0 32 32" aria-hidden="true"><path d="m8 16 6 6 11-13"/></svg>')
            : '<span>'+label+'</span>';
        this.primary.classList.toggle('hold-mode',p.latch);
        this.primary.setAttribute('aria-label',label+(p.latch?' — tap to hold or release':''));
        if (p.latch) this.primary.setAttribute('aria-pressed',String(this.owned.has('touch:latch')));
        else this.primary.removeAttribute('aria-pressed');
    }
    set(source,scans) {
        const want=new Set(scans.map(scan=>source+':'+scan));
        for (const key of [...this.owned]) if (key.startsWith(source+':')&&!want.has(key)) {this.release(key);this.owned.delete(key);}
        for (const scan of scans) {const key=source+':'+scan;this.press(key,scan);this.owned.add(key);}
        this.reflect();
    }
    pulse(scan) {
        const source='touch:accessible:'+scan;this.set(source,[scan]);setTimeout(()=>this.set(source,[]),100);
    }
    bindButton(button) {
        button.addEventListener('pointerdown',event=>{
            if (this.editing||event.button!==0) return;
            event.preventDefault(); button.setPointerCapture(event.pointerId);
            if (button===this.primary&&this.prefs.latch) this.toggleLatch();
            else this.set('touch:button:'+event.pointerId,[Number(button.dataset.scan)]);
        });
        for (const name of ['pointerup','pointercancel','lostpointercapture']) button.addEventListener(name,event=>{
            this.set('touch:button:'+event.pointerId,[]);
            if (name==='pointercancel' && button===this.primary && this.owned.has('touch:latch')) this.toggleLatch();
        });
        button.addEventListener('click',event=>{
            if (event.detail!==0||this.editing) return;
            if (button===this.primary&&this.prefs.latch) this.toggleLatch();else this.pulse(Number(button.dataset.scan));
        });
    }
    toggleLatch() {
        const source='touch:latch';
        if (this.owned.has(source)) {this.release(source);this.owned.delete(source);}
        else {this.press(source,this.prefs.action);this.owned.add(source);}
        this.primary.setAttribute('aria-pressed',String(this.owned.has(source)));this.reflect();
    }
    moveStart(event) {
        if (this.editing||event.button!==0||this.pointers.size) return;
        event.preventDefault();this.moveZone.setPointerCapture(event.pointerId);
        const rect=this.pad.getBoundingClientRect();
        const origin=this.prefs.mode==='stick'?{x:event.clientX,y:event.clientY}:{x:rect.left+rect.width/2,y:rect.top+rect.height/2};
        if (this.prefs.mode==='stick') {
            const zone=this.moveZone.getBoundingClientRect();
            this.pad.style.left=(origin.x-zone.left-rect.width/2)+'px';
            this.pad.style.top=(origin.y-zone.top-rect.height/2)+'px';
        }
        this.pointers.set(event.pointerId,{...origin,sector:null,active:false,radius:rect.width/2});
        this.moveZone.classList.add('active');this.move(event);
    }
    move(event) {
        const p=this.pointers.get(event.pointerId);if (!p) return;
        event.preventDefault();const dx=event.clientX-p.x,dy=event.clientY-p.y,d=Math.hypot(dx,dy);
        const active=d>(p.active?7:12);p.active=active;
        let scans=[];
        if (active) {
            const n=this.prefs.directions,step=2*Math.PI/n,angle=Math.atan2(dy,dx);
            let sector=(Math.round(angle/step)+n)%n;
            if (p.sector!==null) {
                const delta=Math.atan2(Math.sin(angle-p.sector*step),Math.cos(angle-p.sector*step));
                if (Math.abs(delta)<step/2+.12) sector=p.sector;
            }
            p.sector=sector;
            const directions=n===4?[[15],[17],[14],[16]]:[[15],[15,17],[17],[14,17],[14],[14,16],[16],[15,16]];
            scans=directions[sector];
        } else p.sector=null;
        this.set('touch:move',scans);
        const scale=Math.min(1,p.radius*.5/Math.max(1,d));this.knob.style.transform=`translate(${dx*scale}px,${dy*scale}px)`;
    }
    moveEnd(event) {
        if (!this.pointers.delete(event.pointerId)) return;
        this.set('touch:move',[]);this.moveZone.classList.remove('active');this.knob.style.transform='';this.layout();
    }
    reflect() {
        const down=new Set(this.sources.values());
        for (const button of document.querySelectorAll('#touch-deck [data-scan], #phone-keypad [data-scan]')) button.dataset.down=String(down.has(Number(button.dataset.scan)));
    }
    reset() {
        for (const source of this.owned) this.release(source);
        this.owned.clear();this.pointers.clear();this.drag=null;this.moveZone.classList.remove('active');this.knob.style.transform='';
        this.primary.setAttribute('aria-pressed','false');this.layout();this.reflect();
    }
}
window.EkaTouchControls=EkaTouchControls;
