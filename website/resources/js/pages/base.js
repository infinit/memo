function displayBannerMessage(message) {
  $('#popup_notice .message').text(message);
  $('#popup_notice').slideDown();

  setTimeout(function() {
    $('#popup_notice').slideUp();
  }, 3000);
}

$(document).ready(function() {

  $('.icon-slack').magnificPopup({
    type:'inline',
    midClick: true,
    mainClass: 'mfp-fade'
  });

  $('.icon-slack').click(function() {
    $('#slack').show();
  });

  $('.open-popup').magnificPopup({
    type:'inline',
    midClick: true,
    mainClass: 'mfp-fade'
  });

  $('.open-popup').click(function() {
    $($(this).attr('href')).show();
  });

  $('#slack form').submit(function(e) {
    var postData = $(this).serializeArray();
    var formURL = $(this).attr('action');

    $.ajax({
      url: formURL,
      data: postData,
      method: 'post',
    })
    .done(function(response) {
      $.magnificPopup.close();
      displayBannerMessage("Invitation Sent!");
    })
    .fail(function(response) {
      $.magnificPopup.close();
      displayBannerMessage("We couldn't send your invitation, please try again later.");
    });

    e.preventDefault();
    e.unbind();
  });

  if (window.location.hash === '#slack') {
    $.magnificPopup.open({
      items: { src: '#slack'},
      type: 'inline'
    }, 0);

    $('#slack').show();
  }

});