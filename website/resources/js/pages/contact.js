$(document).ready(function() {

  $('body.contact #contact_form').submit(function(e) {
    var postData = $(this).serializeArray();
    var formURL = $(this).attr('action');

    $.ajax({
      url: formURL,
      data: postData,
      method: 'post',
    })
    .done(function(response) {
      displayBannerMessage("Thanks for your message, we'll contact you shortly!");
      $('body.contact #contact_form button').text('Message Sent!');
    })
    .fail(function(response) {
      displayBannerMessage("We couldn't send your message, please try again later.");
    });

    e.preventDefault();
  });

});