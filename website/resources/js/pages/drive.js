$(document).ready(function() {
  if ($('body').hasClass('drive')) {
      $('a.button').click(function() {
        ga('send', 'event', 'download', 'Download Drive', navigator.userAgent);
      });
    }
});