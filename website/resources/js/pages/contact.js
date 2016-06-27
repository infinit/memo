$(document).ready(function() {

  $('#contact_form').submit(function(e) {
    var postData = $(this).serializeArray();
    var formURL = $(this).attr('action');
    $('.btn-send-contact').prop('disabled', true);
    $('.btn-send-contact').addClass('disabled');

    $.ajax({
      url: formURL,
      data: postData,
      method: 'post',
    })
    .done(function(response) {
      displayBannerMessage("Thanks for your message, we'll contact you shortly!");
      $('#contact_form button').text('Message Sent!');
    })
    .fail(function(response) {
      displayBannerMessage("We couldn't send your message, please try again later.");
      $('.btn-send-contact').prop('disabled', false);
      $('.btn-send-contact').removeClass('disabled');
    });

    e.preventDefault();
  });

});