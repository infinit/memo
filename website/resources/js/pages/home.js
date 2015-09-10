$(document).ready(function() {

  $('body.home #commands').t(
    '<ins>1 </ins><mark>$></mark> infinit-storage --create —dropbox —account 455444556 —name dropbox' +
    '<ins>0.5</ins><ins>\nCreated storage “dropbox". \n\n</ins>' +
    '<mark>$></mark> <ins>2</ins>'+

    'infinit-network —create —storage dropbox —storage flickr —name clouds' +
    '<ins>0.5</ins><ins>\nCreated network "clouds". \n\n</ins>' +
    '<mark>$></mark> <ins>2</ins>'+

    'infinit-volume —create —network clouds —name personal-drive' +
    '<ins>0.5</ins><ins>\nStarting network "clouds".'+
    '\nCreating volume root blocks.'+
    '\nCreated volume “personal-drive". \n\n</ins>' +
    '<mark>$></mark> <ins>2</ins>'+

    'infinit-volume —mount —name personal-drive —mountpoint /mnt/personal-drive/' +
    '<ins>0.5</ins><ins>\nMounting volume “personal-drive". \n\n</ins>' +
    '<mark><a href="' + $('.button.try').attr('href') +'">Get started now!</a></mark> ',
    {
      speed: 20,
      speed_vary: true,
      fin: function(elem) {
      }
    }
  );

});