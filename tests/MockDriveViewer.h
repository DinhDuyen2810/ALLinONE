#pragma once

// Trang xem GIẢ LẬP của viewer Google Drive, dùng chung cho các test Downloader (lõi + giao diện): chỉ phục vụ qua
// MiniHttpServer trên 127.0.0.1, không gói tin nào ra khỏi máy.
//
// Dựng đúng cấu trúc DOM mà DriveViewerCapture dựa vào: mỗi trang là một khung `padding-bottom: 56.25%` chứa `<h2>Trang n / N</h2>`
// và một `<img src="blob:...">`. Giống viewer thật ở hai tính chất khó: ảnh được nạp CHẬM khi trang tới gần vùng nhìn (IntersectionObserver)
// và bị GỠ khi cuộn qua (revokeObjectURL) - nên chỉ chụp ngay lúc nạp xong mới có đủ trang. Ảnh trang i rộng 1000+i pixel để test
// kiểm được thứ tự trang trong PDF kết quả.

#include <QByteArray>

inline QByteArray mockDriveViewerHtml(int pages)
{
    return "<!doctype html><meta charset=utf-8><title>Mau.pdf - Google Drive</title>"
           "<style>body{margin:0;background:#444}#sc{height:100vh;overflow:auto}.pg{position:relative;width:80%;margin:24px auto;height:0;background:#fff}"
           ".pg img{position:absolute;left:0;top:0;width:100%;height:100%}.tl{position:absolute;left:0;top:0}h2{font-size:1px;margin:0}</style>"
           "<div id=sc></div><script>const N=" + QByteArray::number(pages) + ",sc=document.getElementById('sc');"
           "function render(i){return new Promise(res=>{const c=document.createElement('canvas');c.width=1000+i;c.height=600;"
           "const g=c.getContext('2d');g.fillStyle='hsl('+(i*30)+',70%,45%)';g.fillRect(0,0,c.width,c.height);g.fillStyle='#fff';"
           "g.font='160px sans-serif';g.fillText('P'+i,60,360);c.toBlob(b=>res(URL.createObjectURL(b)),'image/png');});}"
           "const io=new IntersectionObserver(es=>es.forEach(async e=>{const box=e.target,i=+box.dataset.i;"
           "if(e.isIntersecting){if(box.querySelector('img'))return;await new Promise(r=>setTimeout(r,150+Math.random()*250));if(!box.isConnected)return;"
           "const img=new Image();img.src=await render(i);box.appendChild(img);}"
           "else{const im=box.querySelector('img');if(im){URL.revokeObjectURL(im.src);im.remove();}}}),{root:sc,rootMargin:'50px'});"
           "for(let i=1;i<=N;i++){const d=document.createElement('div');d.className='pg';d.dataset.i=i;d.setAttribute('style','padding-bottom: 56.25%;');"
           "d.innerHTML='<div class=\"x\"></div><div class=\"tl\"><h2 tabindex=0>Trang '+i+' / '+N+'</h2><p>text '+i+'</p></div>';sc.appendChild(d);io.observe(d);}"
           "</script>";
}
