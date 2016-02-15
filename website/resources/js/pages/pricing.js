$(document).ready(function() {

  var usersLabel = $('#usersLabel span');
  var capacityLabel = $('.capacityLabel span');
  var hasAddedStorage = false;

  // Price Update
  function updatePrice() {
    var current_users = parseInt($(usersLabel).text());
    var max_users = $('.plan.startup p.max_users').attr('data-max-users');
    var price_per_user = $('.plan.business p.price').attr('data-price');
    var price = 0;
    var price_storage = 0;

    // yearly price
    if (current_users >= max_users) {
      price =  price_per_user * 12 * current_users;
    }
    console.log('price', price);

    // all storage price
    $.each($('tr.storage'), function(i, element) {
      var current_storage = parseInt($(element).find('.capacityLabel span').text());
      var price_per_gb = $(element).find('.storageSelect option:selected').attr('data-price');

      price_storage += current_storage * 1000 * price_per_gb;
      console.log('storage', current_storage, price_per_gb);
    });


    // full price
    price += price_storage;

    if (price === 0) {
      $('.estimation p.free').show();
      $('.estimation p.infinit').hide();
    } else {
      $('.estimation p.infinit span').text(price);
      $('.estimation p.infinit').show();
      $('.estimation p.free').hide();
    }
  }

  // Users Slider
  $("#usersInput").bind("slider:changed", function (event, data) {
    usersLabel.text(data.value);
    updatePrice();
  });

  // Capacity Slider
  $(".capacityInput").bind("slider:changed", function (event, data) {
    capacityLabel.text(data.value);
    updatePrice();
  });

  $('.storageSelect').on('change', function() {
    updatePrice();
  });

  function bindSlider(element) {
    element.find('.capacityInput').simpleSlider({
      range: [0, 100],
      step: 1,
      value: 1
    });

    // remove duplicated slider - hacky
    if (hasAddedStorage) { element.find('.slider').first().remove(); }

    element.find('.slider').css('min-height', '24px');
    element.find('.dragger').css({'margin-top': '-10px', 'margin-left': '-10px'});

    element.find('.capacityInput').bind("slider:changed", function (event, data) {
      console.log(data);
      element.find('.capacityLabel span').text(data.value);
      updatePrice();
    });

    element.find('.storageSelect').on('change', function() {
      updatePrice();
    });
  }

  $('.add_storage a').on('click', function(event) {
    event.preventDefault();
    var new_element;

    if (!hasAddedStorage) {
      new_element = $( "tr.storage").clone();
    } else {
      new_element = $( "tr.storage").first().clone();
    }

    hasAddedStorage = true;
    bindSlider(new_element);
    $('tr.add_storage').before(new_element);
    updatePrice();
  });

  updatePrice();
  bindSlider($("tr.storage"));

});