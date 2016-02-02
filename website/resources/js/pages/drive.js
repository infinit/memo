$(document).ready(function() {
  if ($('body').hasClass('drive')) {
    $('a.button').click(function() {
      ga('send', 'event', 'download', 'Download Drive', navigator.userAgent);
    });

    $('.linux_popup').magnificPopup({
      type:'inline',
      midClick: true,
      mainClass: 'mfp-fade'
    });

    $('.linux_popup').click(function() {
      $('#install-linux').show();
    });
  }
});