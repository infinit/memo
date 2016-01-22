$(document).ready(function() {

  function launchTerminal() {
    $('body.home #commands').t(
      '<ins>1 </ins><mark>$></mark> infinit-storage --create --s3 --name s3 --capacity 10TB' +
      '<ins>0.5</ins><ins>\nCreated storage "s3". \n\n</ins>' +
      '<mark>$></mark> <ins>2</ins>'+

      'infinit-network --create --storage s3 --storage local --name hybrid-cloud' +
      '<ins>0.5</ins><ins>\nCreated network "hybrid-cloud". \n\n</ins>' +
      '<mark>$></mark> <ins>2</ins>'+

      'infinit-volume --create --network hybrid-cloud --name company' +
      '<ins>0.5</ins><ins>\nStarting network "hybrid-cloud".'+
      '\nCreating volume root blocks.'+
      '\nCreated volume "company". \n\n</ins>' +
      '<mark>$></mark> <ins>2</ins>'+

      'infinit-volume --mount --name company --mountpoint /mnt/company/' +
      '<ins>0.5</ins><ins>\nMounting volume "company". \n\n</ins>' +
      '<mark>$></mark> <ins>2</ins>'+

      'cp -R Engineering/ /mnt/company/'+
      '<ins>0.5</ins><ins>\n\n' +

      '<mark>$></mark> ',
      {
        speed: 20,
        speed_vary: true
      }
    );
  }

  if ($('body').hasClass('home')) {
    var has_reach_terminal = false;

    $(window).scroll(function () {
      if ($(window).scrollTop() > 700 && !has_reach_terminal) {
        has_reach_terminal = true;
        launchTerminal();
      }
    });

    if (window.location.hash === '#slack') {
      // $('#slack').magnificPopup('open');
      $.magnificPopup.open({
        items: { src: '#slack'},
        type: 'inline'
      }, 0);

      $('#slack').show();
    }
  }

});
