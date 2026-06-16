import { initializeApp }
from "https://www.gstatic.com/firebasejs/10.12.2/firebase-app.js";

import {
getDatabase,
ref,
onValue,
set
}
from "https://www.gstatic.com/firebasejs/10.12.2/firebase-database.js";

const firebaseConfig = {

apiKey: "AIzaSyDY2ddXsb1ZdkGB1LmXA3tAdDQX5bPaUYQ",

authDomain: "controle-de-presenca2.firebaseapp.com",

databaseURL:
"https://controle-de-presenca2-default-rtdb.firebaseio.com",

projectId: "controle-de-presenca2",

storageBucket:
"controle-de-presenca2.firebasestorage.app",

messagingSenderId:
"672980279679",

appId:
"1:672980279679:web:8a554229140b669be4ba4a"

};

const app = initializeApp(firebaseConfig);

const db = getDatabase(app);

const pessoasRef = ref(db,"sala/pessoas");

const luzRef = ref(db,"sala/luz");

onValue(pessoasRef,(snapshot)=>{

document
.getElementById("contador")
.innerText = snapshot.val() ?? 0;

});

onValue(luzRef,(snapshot)=>{

const ligada = snapshot.val();

const status =
document.getElementById("status");

if(ligada){

status.innerText = "Ligada";
status.className = "status ligada";

}else{

status.innerText = "Desligada";
status.className = "status desligada";

}

});

document
.getElementById("btnLigar")
.addEventListener("click",()=>{

set(luzRef,true);

});

document
.getElementById("btnDesligar")
.addEventListener("click",()=>{

set(luzRef,false);

});