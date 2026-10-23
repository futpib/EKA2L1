// Touch input shares the keyboard's source ownership, so releasing one finger
// never releases a key still held by another input device.
const EKA_TOUCH_PRESETS = {
    generic: {title:'Custom game', directions:4, actions:[['Select',167]], hold:true},
    snakes: {title:'Snakes', directions:4, horizontal:true,
        actions:[['Boost',53],['Slow',56],['Activate',55],['Hard turn',50]],
        utility:[['Camera',51],['Look left',52],['Look right',54]]},
    'sky-force': {title:'Sky Force', directions:8, actions:[['Secondary',48],['Fire',53]]},
    'k-rally': {title:'K-Rally', directions:4, horizontal:true, hold:true,
        actions:[['Gas',52],['Turbo',55],['Rocket',16],['Mine',17]]},
    'asphalt-2': {title:'Asphalt 2', directions:4, horizontal:true,
        actions:[['Turbo',50],['Brake',48]], utility:[['Gas',53],['Camera',133],['Lights',127]]},
    'hooked-on': {title:'Hooked On', directions:8, actions:[['Cast / Reel',167]]},
    'asphalt-3': {title:'Asphalt 3', directions:4, horizontal:true,
        actions:[['Nitro',53],['Brake',56],['Drift left',55],['Drift right',51]],
        landscape:[53,54,55,49], utility:[['Camera −',133],['Camera +',127]]},
    tetris: {title:'Tetris', directions:4, cardinal:true, horizontal:true,
        actions:[['Rotate ↻',167],['Rotate ↺',53],['Hold',48],['Drop',16]]},
    'super-miners': {title:'Super Miners', directions:4, cardinal:true, actions:[['Action',53]]},
    'n-gage': {title:'N-Gage', directions:4, actions:[['Select',167]]}
};
const EKA_TOUCH_KEYS = [[0,'Hidden'],[167,'Select'],[14,'Left'],[15,'Right'],[16,'Up'],[17,'Down'],
    ...Array.from({length:10},(_,n)=>[48+n,String(n)]),[133,'*'],[127,'#'],[164,'Left softkey'],[165,'Right softkey'],[1,'Clear']];
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
                <div class="menu-keys">
                    <button type="button" data-scan="164" aria-label="Left softkey">L</button>
                    <button type="button" id="menu-select" data-scan="167" aria-label="Select">OK</button>
                    <button type="button" data-scan="165" aria-label="Right softkey">R</button>
                </div>
                <div class="game-actions">${[0,1,2,3].map(i=>`<button type="button" id="${i===0?'primary-action':'action-'+i}" data-action="${i}"></button>`).join('')}</div>
                <div id="utility-actions">${[0,1,2].map(i=>`<button type="button" data-utility="${i}"></button>`).join('')}</div>
            </div></div>`;
        document.body.insertAdjacentHTML('beforeend', `
            <dialog id="touch-settings" aria-labelledby="touch-settings-title">
                <button class="dialog-close" data-close aria-label="Close controls">×</button>
                <h2 id="touch-settings-title">Touch controls</h2><p id="touch-profile"></p>
                <label>Directions <select data-pref="directions"><option value="4">Four directions</option><option value="8">Eight directions</option></select></label>
                <label>Control size <input aria-label="Control size" data-pref="size" type="range" min="120" max="190" step="2"></label>
                <label>Spacing <input aria-label="Spacing" data-pref="gap" type="range" min="4" max="40" step="2"></label>
                <label>Opacity <input aria-label="Opacity" data-pref="opacity" type="range" min="0.35" max="1" step="0.05"></label>
                <label data-haptics>Haptic feedback <input data-pref="haptics" type="checkbox"></label>
                <label>Left-handed layout <input data-pref="leftHanded" type="checkbox"></label>
                <label data-orientation>Game controls <select data-pref="orientation"><option value="auto">Auto (game display)</option><option value="portrait">Portrait bindings</option><option value="landscape">Landscape 270 bindings</option></select></label>
                <div id="action-bindings">${[0,1,2,3].map(i=>`<label><span data-binding-label="${i}"></span><select data-binding="${i}">${EKA_TOUCH_KEYS.map(([scan,label])=>`<option value="${scan}">${label}</option>`).join('')}</select></label>`).join('')}</div>
                <label data-utility-setting>Show extra controls <input data-pref="utility" type="checkbox"></label>
                <label data-hold-setting>Tap primary action to hold / release <input data-pref="latch" type="checkbox"></label>
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
        this.primary = document.getElementById('primary-action');
        this.actionButtons = [...this.actions.querySelectorAll('[data-action]')];
        this.utilityButtons = [...this.actions.querySelectorAll('[data-utility]')];
        this.canvas = document.getElementById('canvas'); this.knob = document.getElementById('stick-knob');
        for (const [label, scan] of [['1',49],['2',50],['3',51],['4',52],['5',53],['6',54],['7',55],['8',56],['9',57],['*',133],['0',48],['#',127],['Left softkey',164],['Select',167],['Right softkey',165],['Clear',1]]) {
            const button = document.createElement('button'); button.type = 'button'; button.textContent = label;
            button.dataset.scan = scan; this.keypad.querySelector('.key-grid').append(button);
        }
        document.getElementById('btn-touch-settings').onclick = () => {
            if (this.editing) { this.setEditing(false); return; }
            this.open(this.settings);
        };
        document.getElementById('btn-keypad').onclick = () => { this.setEditing(false); this.open(this.keypad); };
        this.menuButton.onclick = event => {
            if(event.detail!==0)return;
            if (this.editing) this.setEditing(false); else this.open(this.menu);
        };
        // Handle pointer taps directly, including outside the visible menu circle.
        // Keep keyboard/assistive clicks; suppress duplicate native clicks after pointer taps.
        this.menuButton.addEventListener('pointerdown',event=>{
            if(event.button!==0||this.menuPointer!=null)return;
            event.preventDefault();this.menuPointer=event.pointerId;
            this.menuButton.setPointerCapture(event.pointerId);this.haptic(event);
        });
        this.menuButton.addEventListener('pointerup',event=>{
            if(this.menuPointer!==event.pointerId)return;
            this.menuPointer=null;
            const r=this.menuButton.getBoundingClientRect();
            if(event.clientX>=r.left-12&&event.clientX<=r.right+12
                &&event.clientY>=r.top-12&&event.clientY<=r.bottom+12)this.menuButton.click();
        });
        for(const name of ['pointercancel','lostpointercapture'])this.menuButton.addEventListener(name,event=>{
            if(this.menuPointer===event.pointerId)this.menuPointer=null;
        });
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
                : input.dataset.pref==='orientation' ? input.value : Number(input.value);
            this.syncOrientation(); this.save(); this.fillSettings(); this.layout();
        }));
        this.settings.querySelectorAll('[data-binding]').forEach(input=>input.addEventListener('input',()=>{
            this.releaseAll(); this.bindings[Number(input.dataset.binding)]=Number(input.value);
            if(!this.canLatch)this.prefs.latch=false;
            this.save(); this.fillSettings(); this.layout();
        }));
        document.getElementById('reset-touch-layout').onclick = () => {
            this.releaseAll(); this.prefs = this.defaults(); this.syncOrientation(); this.save(); this.fillSettings(); this.layout();
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
        new MutationObserver(()=>this.syncOrientation()).observe(this.canvas,{attributes:true,attributeFilter:['width','height']});
        this.configure('');
        this.menu.showModal();
    }
    clamp(value,min,max) { return Math.min(max,Math.max(min,value)); }
    defaults() {
        const scans=this.preset.actions.map(action=>action[1]);
        while(scans.length<4)scans.push(0);
        return {directions:this.preset.directions,size:132,gap:12,opacity:.65,leftHanded:false,haptics:true,
            bindings:{portrait:scans,landscape:this.preset.landscape?.slice()||scans.slice()},
            orientation:'auto',utility:false,latch:false,move:{x:.25,y:.75},buttons:{x:.75,y:.92}};
    }
    configure(app) {
        this.releaseAll();
        const name=String(app).trim().toLowerCase();
        const aliases={'0x2000730f':'snakes','0xa020d913':'sky-force','sky force':'sky-force',
            '0xa0000bf6':'k-rally','0x2001302c':'asphalt-2','0xa0000bf4':'super-miners'};
        this.profile=(Object.hasOwn(aliases,name)?aliases[name]:null)||(Object.hasOwn(EKA_TOUCH_PRESETS,name)?name:'generic');
        this.preset=EKA_TOUCH_PRESETS[this.profile];
        this.storageKey='eka-touch-v2:'+(this.profile==='generic'?(name||'generic'):this.profile);
        this.prefs=this.defaults();
        try {
            const current=localStorage.getItem(this.storageKey);
            // Old shared N-Gage preferences cannot identify which game they belong to.
            const legacyName=({'asphalt-2':'0x2001302c','super-miners':'0xa0000bf4'})[this.profile]||this.profile;
            const legacyKey='eka-touch-v1:'+(this.profile==='generic'?(name||'generic'):legacyName);
            const saved=JSON.parse(current||localStorage.getItem(legacyKey));
            if(saved&&typeof saved==='object') {
                for(const [key,min,max] of [['size',120,190],['gap',4,40],['opacity',.35,1]])
                    if(Number.isFinite(saved[key]))this.prefs[key]=this.clamp(saved[key],min,max);
                for(const key of ['leftHanded','haptics','utility'])if(typeof saved[key]==='boolean')this.prefs[key]=saved[key];
                for(const key of ['move','buttons'])if(Number.isFinite(saved[key]?.x)&&Number.isFinite(saved[key]?.y))
                    this.prefs[key]={x:this.clamp(saved[key].x,0,1),y:this.clamp(saved[key].y,0,1)};
                if(current) {
                    if([4,8].includes(saved.directions)&&!this.preset.cardinal)this.prefs.directions=saved.directions;
                    if(['auto','portrait','landscape'].includes(saved.orientation))this.prefs.orientation=saved.orientation;
                    for(const orientation of ['portrait','landscape'])for(let i=0;i<4;i++) {
                        const scan=saved.bindings?.[orientation]?.[i];
                        if(EKA_TOUCH_KEYS.some(key=>key[0]===scan))this.prefs.bindings[orientation][i]=scan;
                    }
                    if(this.preset.hold&&typeof saved.latch==='boolean')this.prefs.latch=saved.latch;
                } else if(this.profile==='generic') {
                    const scan=({141:53,146:48})[saved.action]??saved.action;
                    if(EKA_TOUCH_KEYS.some(key=>key[0]===scan))for(const orientation of ['portrait','landscape'])this.prefs.bindings[orientation][0]=scan;
                }
            }
        } catch (_) { /* Private browsing and unavailable storage retain usable defaults. */ }
        this.orientation=this.controlOrientation(); this.fillSettings(); this.layout();
    }
    controlOrientation() {
        if(this.profile!=='asphalt-3')return 'portrait';
        return this.prefs.orientation==='auto'?(this.canvas.width>this.canvas.height?'landscape':'portrait'):this.prefs.orientation;
    }
    syncOrientation() {
        if(!this.prefs)return;
        const orientation=this.controlOrientation();
        if(orientation===this.orientation)return;
        this.releaseAll(); this.orientation=orientation; this.fillSettings(); this.layout();
    }
    get canLatch() { return !!this.preset.hold&&(this.profile!=='k-rally'||this.bindings[0]===52); }
    get bindings() { return this.prefs.bindings[this.orientation]; }
    get primaryScan() { this.syncOrientation(); return this.bindings[0]||167; }
    save() { try { localStorage.setItem(this.storageKey,JSON.stringify(this.prefs)); } catch (_) {} }
    startGame(app) {
        this.configure(app); this.menu.close();
        this.menu.querySelector('[data-close]').hidden=false;
        this.menuButton.hidden=false;
        document.getElementById('session-title').textContent=this.preset.title;
    }
    fillSettings() {
        document.getElementById('touch-profile').textContent=this.preset.title+' · Saved on this device';
        this.settings.querySelector('[data-haptics]').hidden=typeof navigator.vibrate!=='function';
        this.settings.querySelector('[data-orientation]').hidden=this.profile!=='asphalt-3';
        this.settings.querySelector('[data-utility-setting]').hidden=!this.preset.utility;
        this.settings.querySelector('[data-hold-setting]').hidden=!this.canLatch;
        this.settings.querySelector('[data-pref="directions"]').disabled=!!this.preset.cardinal;
        for (const input of this.settings.querySelectorAll('[data-pref]')) {
            if (input.type==='checkbox') input.checked=this.prefs[input.dataset.pref]; else input.value=this.prefs[input.dataset.pref];
        }
        for(let i=0;i<4;i++) {
            this.settings.querySelector(`[data-binding-label="${i}"]`).textContent=this.preset.actions[i]?.[0]||'Extra action '+(i+1);
            this.settings.querySelector(`[data-binding="${i}"]`).value=this.bindings[i];
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
        this.deck.dataset.profile=this.profile;
        this.deck.classList.toggle('left-handed',p.leftHanded);
        root.style.setProperty('--control-size',p.size+'px'); root.style.setProperty('--control-gap',p.gap+'px'); root.style.setProperty('--control-opacity',p.opacity);
        this.moveZone.style.order=p.leftHanded?2:0;
        const extras=!!(p.utility&&this.preset.utility);
        this.actions.querySelector('#utility-actions').hidden=!extras;
        for (const [group,zone,key] of [[this.pad,this.moveZone,'move'],[this.actions,this.actionZone,'buttons']]) {
            const w=zone.clientWidth,h=zone.clientHeight;
            const size=key==='move'?Math.max(1,Math.min(p.size,w,h)):Math.max(132,Math.min(p.size*1.2,w,h-52-(extras?52:0)));
            const height=key==='move'?size:size+52+(extras?52:0);
            group.style.setProperty('--actual-size',size+'px'); group.style.height=height+'px';
            group.style.left=Math.max(0,w-size)*p[key].x+'px'; group.style.top=Math.max(0,h-height)*p[key].y+'px';
        }
        for(let i=0;i<4;i++) {
            const button=this.actionButtons[i],scan=this.bindings[i],key=EKA_TOUCH_KEYS.find(k=>k[0]===scan)?.[1];
            const label=this.preset.actions[i]?.[0]||key;
            button.hidden=!scan; button.dataset.scan=scan; button.textContent=label;
            button.setAttribute('aria-label',label+' ('+key+')'); button.title=key;
        }
        for(let i=0;i<3;i++) {
            const action=this.preset.utility?.[i],button=this.utilityButtons[i];
            button.hidden=!action;button.dataset.scan=action?.[1]||0;button.textContent=action?.[0]||'';
        }
        this.primary.classList.toggle('hold-mode',p.latch&&this.canLatch);
        if(p.latch&&this.canLatch)this.primary.setAttribute('aria-pressed',String(this.owned.has('touch:latch')));
        else this.primary.removeAttribute('aria-pressed');
        this.reflect();
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
    haptic(event) {
        if (event.pointerType!=='touch'||!this.prefs.haptics||document.hidden||typeof navigator.vibrate!=='function') return;
        const now=performance.now();
        if (now-(this.lastHaptic ?? -Infinity)<40) return;
        this.lastHaptic=now;
        try {navigator.vibrate(8);} catch (_) {}
    }
    buttonStart(button,event) {
        if (this.editing||event.button!==0) return;
        this.syncOrientation();
        event.preventDefault(); button.setPointerCapture(event.pointerId);
        this.haptic(event);
        if (button===this.primary&&this.prefs.latch&&this.canLatch) this.toggleLatch();
        else this.set('touch:button:'+event.pointerId,[Number(button.dataset.scan)]);
    }
    bindButton(button) {
        button.addEventListener('pointerdown',event=>this.buttonStart(button,event));
        for (const name of ['pointerup','pointercancel','lostpointercapture']) button.addEventListener(name,event=>{
            this.set('touch:button:'+event.pointerId,[]);
            if (name==='pointercancel' && button===this.primary && this.owned.has('touch:latch')) this.toggleLatch();
        });
        button.addEventListener('click',event=>{
            if (event.detail!==0||this.editing) return;
            if (button===this.primary&&this.prefs.latch&&this.canLatch) this.toggleLatch();else this.pulse(Number(button.dataset.scan));
        });
    }
    toggleLatch() {
        const source='touch:latch';
        if (this.owned.has(source)) {this.release(source);this.owned.delete(source);}
        else {this.press(source,this.primaryScan);this.owned.add(source);}
        this.primary.setAttribute('aria-pressed',String(this.owned.has(source)));this.reflect();
    }
    controlTarget(x,y) {
        // Resolve overlapping 12px target extensions by distance, never DOM stacking.
        let nearest=null,distance=Infinity,centreDistance=Infinity;
        for(const button of this.actions.querySelectorAll('button')) {
            const r=button.getBoundingClientRect();if(!r.width||!r.height)continue;
            const dx=Math.max(r.left-x,0,x-r.right),dy=Math.max(r.top-y,0,y-r.bottom);
            if(dx>12||dy>12)continue;
            const d=dx*dx+dy*dy,c=(x-r.x-r.width/2)**2+(y-r.y-r.height/2)**2;
            if(d<distance||(d===distance&&c<centreDistance)) {
                nearest=button;distance=d;centreDistance=c;
            }
        }
        return nearest;
    }
    moveStart(event) {
        if (this.editing||this.modalOpen||event.button!==0) return;
        this.syncOrientation();event.preventDefault();
        const button=this.controlTarget(event.clientX,event.clientY);
        if(button) {this.buttonStart(button,event);return;}
        // Near misses and gaps in the action cluster stay inert for this contact.
        const blocked=[this.actions,this.menuButton].some(control=>{
            const r=control.getBoundingClientRect();
            return r.width&&r.height&&event.clientX>=r.left-24&&event.clientX<=r.right+24
                &&event.clientY>=r.top-24&&event.clientY<=r.bottom+24;
        });
        if(blocked||this.pointers.size)return;
        this.moveZone.setPointerCapture(event.pointerId);
        const rect=this.pad.getBoundingClientRect();
        const origin={x:event.clientX,y:event.clientY},zone=this.moveZone.getBoundingClientRect();
        this.pad.style.left=(origin.x-zone.left-rect.width/2)+'px';
        this.pad.style.top=(origin.y-zone.top-rect.height/2)+'px';
        this.haptic(event);
        this.pointers.set(event.pointerId,{...origin,sector:null,active:false,radius:rect.width/2});
        this.moveZone.classList.add('active');this.move(event);
    }
    move(event) {
        const p=this.pointers.get(event.pointerId);if (!p) return;
        event.preventDefault();const dx=event.clientX-p.x,dy=p.horizontal?0:event.clientY-p.y,d=Math.hypot(dx,dy);
        const active=d>(p.active?7:12),previousSector=p.sector;p.active=active;
        let scans=[];
        if (active) {
            const n=this.preset.cardinal?4:this.prefs.directions,step=2*Math.PI/n,angle=Math.atan2(dy,dx);
            let sector=(Math.round(angle/step)+n)%n;
            if (p.sector!==null) {
                const delta=Math.atan2(Math.sin(angle-p.sector*step),Math.cos(angle-p.sector*step));
                if (Math.abs(delta)<step/2+.12) sector=p.sector;
            }
            if(this.preset.horizontal&&(sector===0||sector===n/2))p.horizontal=true;
            p.sector=sector;
            const directions=n===4?[[15],[17],[14],[16]]:[[15],[15,17],[17],[14,17],[14],[14,16],[16],[15,16]];
            scans=directions[sector];
        } else p.sector=null;
        if (p.sector!==null&&p.sector!==previousSector) this.haptic(event);
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
        this.owned.clear();this.pointers.clear();this.drag=null;this.menuPointer=null;this.moveZone.classList.remove('active');this.knob.style.transform='';
        this.primary.setAttribute('aria-pressed','false');this.layout();this.reflect();
    }
}
window.EkaTouchControls=EkaTouchControls;
